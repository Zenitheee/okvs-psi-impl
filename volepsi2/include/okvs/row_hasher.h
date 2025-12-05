// Row generation for the OKVS reproduction.

#pragma once

#include "okvs/gf128.h"

#include <cstddef>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace okvs {

struct RowData {
    std::vector<std::uint32_t> sparse;
    std::vector<GF128> dense;
};

class RowHasher {
public:
    RowHasher(std::size_t mPrime, std::size_t denseCols, std::size_t weight, std::uint64_t seed);

    RowData generate(const std::string& key) const;
    RowData generate(std::span<const std::uint8_t> keyBytes) const;

    [[nodiscard]] std::size_t sparseSize() const { return mMPrime; }
    [[nodiscard]] std::size_t denseSize() const { return mDenseCols; }
    [[nodiscard]] std::size_t weight() const { return mWeight; }

private:
    std::size_t mMPrime;
    std::size_t mDenseCols;
    std::size_t mWeight;
    std::uint64_t mSeed;

    static std::vector<std::uint32_t> sampleDistinct(std::mt19937_64& rng, std::size_t limit, std::size_t count);
};

} // namespace okvs

