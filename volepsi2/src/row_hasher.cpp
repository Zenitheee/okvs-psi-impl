#include "okvs/row_hasher.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace okvs {

RowHasher::RowHasher(std::size_t mPrime, std::size_t denseCols, std::size_t weight, std::uint64_t seed)
    : mMPrime(mPrime),
      mDenseCols(denseCols),
      mWeight(weight),
      mSeed(seed) {}

std::vector<std::uint32_t> RowHasher::sampleDistinct(std::mt19937_64& rng, std::size_t limit, std::size_t count) {
    if (count > limit) {
        throw std::runtime_error("Cannot sample more distinct positions than available columns.");
    }

    std::unordered_set<std::uint32_t> chosen;
    std::uniform_int_distribution<std::uint64_t> dist(0, limit - 1);
    while (chosen.size() < count) {
        chosen.insert(static_cast<std::uint32_t>(dist(rng)));
    }

    std::vector<std::uint32_t> result(chosen.begin(), chosen.end());
    std::sort(result.begin(), result.end());
    return result;
}

namespace {

std::vector<std::uint32_t> buildSeedMaterial(std::span<const std::uint8_t> keyBytes, std::uint64_t seed)
{
    std::vector<std::uint32_t> seedMaterial;
    seedMaterial.reserve(keyBytes.size() / 4 + 4);
    seedMaterial.push_back(static_cast<std::uint32_t>(seed));
    seedMaterial.push_back(static_cast<std::uint32_t>(seed >> 32));

    std::uint32_t acc = 0;
    int shift = 0;
    for (auto byte : keyBytes) {
        acc |= static_cast<std::uint32_t>(byte) << shift;
        shift += 8;
        if (shift == 32) {
            seedMaterial.push_back(acc);
            acc = 0;
            shift = 0;
        }
    }
    if (shift != 0) {
        seedMaterial.push_back(acc);
    }
    if (seedMaterial.empty()) {
        seedMaterial.push_back(0);
    }

    return seedMaterial;
}

} // namespace

RowData RowHasher::generate(const std::string& key) const {
    const auto* data = key.empty() ? nullptr : key.data();
    return generate(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(data), key.size()));
}

RowData RowHasher::generate(std::span<const std::uint8_t> keyBytes) const {
    auto seedMaterial = buildSeedMaterial(keyBytes, mSeed);

    std::seed_seq seq(seedMaterial.begin(), seedMaterial.end());
    std::mt19937_64 rng(seq);

    RowData row;
    row.sparse = sampleDistinct(rng, mMPrime, mWeight);

    if (mDenseCols > 0) {
        GF128 base;
        do {
            uint64_t lo = rng();
            uint64_t hi = rng();
            base = GF128(hi, lo);
        } while (base.isZero());

        row.dense.resize(mDenseCols);
        GF128 power = base;
        for (std::size_t i = 0; i < mDenseCols; ++i) {
            row.dense[i] = power;
            power *= base;
        }
    }

    return row;
}

} // namespace okvs

