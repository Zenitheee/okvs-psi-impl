#pragma once

#include "okvs/encoder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace okvs {

class BinnedOkvsEncoder {
public:
    BinnedOkvsEncoder(OkvsConfig config,
                      std::uint64_t seed,
                      std::size_t numItems,
                      std::size_t binSizeHint);

    [[nodiscard]] std::size_t totalSize() const;
    [[nodiscard]] std::size_t numBins() const;
    [[nodiscard]] std::size_t itemsPerBin() const;
    [[nodiscard]] std::size_t sparsePerBin() const;
    [[nodiscard]] std::size_t densePerBin() const;

    void encode(std::span<const KeyView> keys,
                std::span<const GF128> values,
                std::span<GF128> output,
                std::size_t numThreads = 1) const;

    [[nodiscard]] GF128 decode(KeyView key, std::span<const GF128> table) const;
    void decode(std::span<const KeyView> keys,
                std::span<GF128> values,
                std::span<const GF128> table,
                std::size_t numThreads = 1) const;

private:
    static std::size_t computeNumBins(std::size_t numItems, std::size_t binSizeHint);
    static std::size_t computeItemsPerBin(std::size_t numItems, std::size_t numBins, std::size_t ssp);
    static std::uint64_t hashKey(KeyView key, std::uint64_t seed);

    [[nodiscard]] std::size_t clusterForKey(KeyView key) const;
    [[nodiscard]] std::size_t sparseOffset(std::size_t cluster) const;
    [[nodiscard]] std::size_t denseBaseOffset() const;
    [[nodiscard]] std::size_t denseOffset(std::size_t cluster) const;

    OkvsConfig mConfig;
    std::uint64_t mSeed = 0;
    std::size_t mNumItems = 0;
    std::size_t mNumBins = 1;
    std::size_t mItemsPerBin = 0;
    std::size_t mSparsePerBin = 0;
    std::size_t mDensePerBin = 0;
    std::size_t mTotalSparseSize = 0;
    std::size_t mTotalDenseSize = 0;
    std::size_t mClusterTableSize = 0;
    OkvsEncoder mClusterEncoder;
};

} // namespace okvs
