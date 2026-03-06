#include "okvs/psi.h"

#include "hash_utils.h"
#include "okvs/binned_encoder.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace okvs {
namespace {

constexpr std::uint64_t kHBSeed0 = 0x5b1f3d8a6c2e9071ULL;
constexpr std::uint64_t kHBSeed1 = 0x94d049bb133111ebULL;
constexpr std::uint64_t kHOSeed0 = 0xbf58476d1ce4e5b9ULL;
constexpr std::uint64_t kHOSeed1 = 0x9e3779b97f4a7c15ULL;

std::span<const std::uint8_t> asBytes(KeyView key) {
    if (key.size == 0 || key.data == nullptr) {
        return {};
    }
    return std::span<const std::uint8_t>(key.data, key.size);
}

class SessionRng {
public:
    explicit SessionRng(std::uint64_t seed)
        : mState(seed) {}

    std::uint64_t nextU64() {
        mState += 0x9e3779b97f4a7c15ULL;
        return internal::splitMix64(mState);
    }

    GF128 nextField() {
        return GF128(nextU64(), nextU64());
    }

private:
    std::uint64_t mState;
};

struct GF128Hash {
    std::size_t operator()(const GF128& value) const {
        const auto mixed = internal::splitMix64(value.lo() ^ std::rotl(value.hi(), 23));
        return static_cast<std::size_t>(mixed);
    }
};

GF128 hashToBaseField(KeyView key, const GF128& salt) {
    return internal::hashBytesToField(
        asBytes(key),
        salt.lo() ^ kHBSeed0,
        salt.hi() ^ kHBSeed1);
}

GF128 hashOutput(const GF128& value) {
    return internal::hashFieldToField(value, kHOSeed0, kHOSeed1);
}

class ProtocolOkvs {
public:
    ProtocolOkvs(const PsiConfig& config, std::size_t receiverSize, std::uint64_t seed)
        : mUseClustering(config.binSizeHint != 0 && receiverSize > config.binSizeHint),
          mNumThreads(std::max<std::size_t>(1, config.numThreads)) {
        if (mUseClustering) {
            mBinned = std::make_unique<BinnedOkvsEncoder>(
                config.okvs,
                seed,
                receiverSize,
                config.binSizeHint);
        } else {
            mEncoder = std::make_unique<OkvsEncoder>(config.okvs, seed);
            mSparseColumns = mEncoder->sparseSize(receiverSize);
            mDenseColumns = mEncoder->denseSize(receiverSize);
        }
    }

    [[nodiscard]] std::size_t tableSize() const {
        if (mUseClustering) {
            return mBinned->totalSize();
        }
        return mSparseColumns + mDenseColumns;
    }

    [[nodiscard]] bool usesClustering() const {
        return mUseClustering;
    }

    [[nodiscard]] std::vector<GF128> encode(std::span<const KeyView> keys,
                                            std::span<const GF128> values) const {
        if (mUseClustering) {
            std::vector<GF128> table(mBinned->totalSize(), GF128::zero());
            mBinned->encode(keys, values, std::span<GF128>(table.data(), table.size()), mNumThreads);
            return table;
        }

        auto table = mEncoder->encode(keys, values);
        return table.data;
    }

    void decode(std::span<const KeyView> keys,
                std::span<GF128> values,
                std::span<const GF128> table) const {
        if (mUseClustering) {
            mBinned->decode(keys, values, table, mNumThreads);
            return;
        }

        OkvsEncoder::EncodedTableView view{table, mSparseColumns, mDenseColumns};
        mEncoder->decode(keys, values, view, mNumThreads);
    }

private:
    bool mUseClustering = false;
    std::size_t mNumThreads = 1;
    std::size_t mSparseColumns = 0;
    std::size_t mDenseColumns = 0;
    std::unique_ptr<OkvsEncoder> mEncoder;
    std::unique_ptr<BinnedOkvsEncoder> mBinned;
};

} // namespace

SemiHonestPsi::SemiHonestPsi(PsiConfig config)
    : mConfig(std::move(config)) {}

PsiResult SemiHonestPsi::run(std::span<const KeyView> receiverSet,
                             std::span<const KeyView> senderSet) const {
    PsiResult result;
    if (receiverSet.empty()) {
        return result;
    }

    SessionRng rng(
        mConfig.seed ^
        (static_cast<std::uint64_t>(receiverSet.size()) << 32) ^
        static_cast<std::uint64_t>(senderSet.size()));

    const auto okvsSeed = rng.nextU64();
    const auto valueSeed = rng.nextField();

    ProtocolOkvs okvs(mConfig, receiverSet.size(), okvsSeed);
    result.okvsSize = okvs.tableSize();
    result.usedClustering = okvs.usesClustering();

    std::vector<GF128> baseValues(receiverSet.size(), GF128::zero());
    for (std::size_t i = 0; i < receiverSet.size(); ++i) {
        baseValues[i] = hashToBaseField(receiverSet[i], valueSeed);
    }

    const auto p = okvs.encode(receiverSet, std::span<const GF128>(baseValues.data(), baseValues.size()));

    std::vector<GF128> a(result.okvsSize, GF128::zero());
    std::vector<GF128> b(result.okvsSize, GF128::zero());
    std::vector<GF128> c(result.okvsSize, GF128::zero());
    const auto delta = rng.nextField();

    for (std::size_t i = 0; i < result.okvsSize; ++i) {
        a[i] = rng.nextField();
        b[i] = rng.nextField();
        c[i] = a[i] * delta + b[i];
    }

    std::vector<GF128> bPrime(result.okvsSize, GF128::zero());
    for (std::size_t i = 0; i < result.okvsSize; ++i) {
        // In GF(2^128), subtraction equals addition.
        const auto correction = a[i] + p[i];
        bPrime[i] = b[i] + correction * delta;
    }

    std::vector<GF128> receiverDecoded(receiverSet.size(), GF128::zero());
    okvs.decode(receiverSet, std::span<GF128>(receiverDecoded.data(), receiverDecoded.size()), c);

    std::vector<GF128> senderDecoded(senderSet.size(), GF128::zero());
    okvs.decode(senderSet, std::span<GF128>(senderDecoded.data(), senderDecoded.size()), bPrime);

    std::vector<GF128> senderTags(senderSet.size(), GF128::zero());
    for (std::size_t i = 0; i < senderSet.size(); ++i) {
        const auto senderValue = senderDecoded[i] + delta * hashToBaseField(senderSet[i], valueSeed);
        senderTags[i] = hashOutput(senderValue);
    }

    std::shuffle(senderTags.begin(), senderTags.end(), std::mt19937_64(rng.nextU64()));

    std::unordered_set<GF128, GF128Hash> senderTagSet;
    senderTagSet.reserve(senderTags.size());
    for (const auto& tag : senderTags) {
        senderTagSet.insert(tag);
    }

    for (std::size_t i = 0; i < receiverDecoded.size(); ++i) {
        if (senderTagSet.contains(hashOutput(receiverDecoded[i]))) {
            result.intersectionIndices.push_back(i);
        }
    }

    return result;
}

} // namespace okvs
