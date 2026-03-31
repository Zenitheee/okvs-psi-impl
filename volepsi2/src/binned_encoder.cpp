#include "okvs/binned_encoder.h"

#include "hash_utils.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>

namespace okvs {
namespace {

} // namespace

BinnedOkvsEncoder::BinnedOkvsEncoder(OkvsConfig config,
                                     std::uint64_t seed,
                                     std::size_t numItems,
                                     std::size_t binSizeHint)
    : mConfig(config),
      mSeed(seed),
      mNumItems(numItems),
      mNumBins(computeNumBins(numItems, binSizeHint)),
      mItemsPerBin(computeItemsPerBin(numItems, mNumBins, config.securityParameter)),
      mBinEncoder([&] {
          OkvsConfig fixed = config;
          OkvsEncoder probe(config, seed);
          fixed.explicitSparseSize = probe.sparseSize(mItemsPerBin);
          fixed.explicitDenseSize = probe.denseSize(mItemsPerBin);
          return OkvsEncoder(fixed, seed);
      }()) {
    mSparsePerBin = mBinEncoder.sparseSize(mItemsPerBin);
    mDensePerBin = mBinEncoder.denseSize(mItemsPerBin);
    mSizePerBin = mSparsePerBin + mDensePerBin;
}

std::size_t BinnedOkvsEncoder::totalSize() const { return mNumBins * mSizePerBin; }
std::size_t BinnedOkvsEncoder::numBins() const { return mNumBins; }
std::size_t BinnedOkvsEncoder::itemsPerBin() const { return mItemsPerBin; }
std::size_t BinnedOkvsEncoder::sparsePerBin() const { return mSparsePerBin; }
std::size_t BinnedOkvsEncoder::densePerBin() const { return mDensePerBin; }

std::size_t BinnedOkvsEncoder::computeNumBins(std::size_t numItems, std::size_t binSizeHint) {
    if (numItems == 0) return 1;
    if (binSizeHint == 0) return 1;
    return std::max<std::size_t>(1, (numItems + binSizeHint - 1) / binSizeHint);
}

std::size_t BinnedOkvsEncoder::computeItemsPerBin(std::size_t numItems, std::size_t numBins, std::size_t ssp) {
    if (numItems == 0) return 1;
    if (numBins == 0) throw std::runtime_error("numBins must be non-zero.");

    const double avg = static_cast<double>(numItems) / static_cast<double>(numBins);
    const double sigma = std::sqrt(std::max(1.0, avg));
    const double safety = 6.0 * sigma + static_cast<double>(std::max<std::size_t>(40, ssp));
    const auto cap = static_cast<std::size_t>(std::ceil(avg + safety));
    return std::max<std::size_t>(1, cap);
}

std::uint64_t BinnedOkvsEncoder::hashKey(KeyView key, std::uint64_t seed) {
    const auto bytes = (key.size == 0 || key.data == nullptr)
        ? std::span<const std::uint8_t>()
        : std::span<const std::uint8_t>(key.data, key.size);
    return internal::hashBytesToU64(
        bytes,
        seed,
        0x38b34ae59d1228d9ULL,
        internal::HashDomain::BinnedKey);
}

void BinnedOkvsEncoder::encode(std::span<const KeyView> keys,
                               std::span<const GF128> values,
                               std::span<GF128> output,
                               std::size_t numThreads) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/value vector sizes.");
    }
    if (output.size() != totalSize()) {
        throw std::runtime_error("Output buffer size mismatch for binned encoding.");
    }

    std::fill(output.begin(), output.end(), GF128::zero());

    std::vector<std::vector<std::size_t>> bins(mNumBins);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].size > 0 && keys[i].data == nullptr) {
            throw std::runtime_error("KeyView points to null data.");
        }
        const auto b = hashKey(keys[i], mSeed) % mNumBins;
        bins[b].push_back(i);
    }

    for (std::size_t b = 0; b < mNumBins; ++b) {
        if (bins[b].size() > mItemsPerBin) {
            throw std::runtime_error("Bin overflow in clustered OKVS encoder.");
        }
    }

    const std::size_t threads = std::max<std::size_t>(1, numThreads);
    std::atomic<std::size_t> nextBin{0};
    std::exception_ptr workerError = nullptr;

    auto worker = [&]() {
        try {
            while (true) {
                const auto b = nextBin.fetch_add(1, std::memory_order_relaxed);
                if (b >= mNumBins) {
                    break;
                }

                const auto count = bins[b].size();
                if (count == 0) {
                    continue;
                }

                std::vector<KeyView> binKeys(count);
                std::vector<GF128> binValues(count);
                for (std::size_t j = 0; j < count; ++j) {
                    const auto idx = bins[b][j];
                    binKeys[j] = keys[idx];
                    binValues[j] = values[idx];
                }

                auto table = mBinEncoder.encode(
                    std::span<const KeyView>(binKeys.data(), binKeys.size()),
                    std::span<const GF128>(binValues.data(), binValues.size()));
                if (table.data.size() != mSizePerBin) {
                    throw std::runtime_error("Unexpected per-bin encoded size.");
                }

                auto outBin = output.subspan(b * mSizePerBin, mSizePerBin);
                std::copy(table.data.begin(), table.data.end(), outBin.begin());
            }
        } catch (...) {
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    };

    if (threads == 1 || mNumBins == 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) {
            pool.emplace_back(worker);
        }
        for (auto& th : pool) {
            th.join();
        }
    }

    if (workerError) {
        std::rethrow_exception(workerError);
    }
}

GF128 BinnedOkvsEncoder::decode(KeyView key, std::span<const GF128> table) const {
    if (table.size() != totalSize()) {
        throw std::runtime_error("Binned table size mismatch.");
    }
    if (key.size > 0 && key.data == nullptr) {
        throw std::runtime_error("KeyView points to null data.");
    }

    const auto b = hashKey(key, mSeed) % mNumBins;
    auto slice = table.subspan(b * mSizePerBin, mSizePerBin);
    OkvsEncoder::EncodedTableView view{slice, mSparsePerBin, mDensePerBin};
    return mBinEncoder.decode(key, view);
}

void BinnedOkvsEncoder::decode(std::span<const KeyView> keys,
                               std::span<GF128> values,
                               std::span<const GF128> table,
                               std::size_t numThreads) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/output sizes for binned decode.");
    }

    const std::size_t threads = std::max<std::size_t>(1, numThreads);
    std::atomic<std::size_t> next{0};
    std::exception_ptr workerError = nullptr;

    auto worker = [&]() {
        try {
            while (true) {
                const auto i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= keys.size()) {
                    break;
                }
                values[i] = decode(keys[i], table);
            }
        } catch (...) {
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    };

    if (threads == 1 || keys.size() < 2) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) {
            pool.emplace_back(worker);
        }
        for (auto& th : pool) {
            th.join();
        }
    }

    if (workerError) {
        std::rethrow_exception(workerError);
    }
}

} // namespace okvs
