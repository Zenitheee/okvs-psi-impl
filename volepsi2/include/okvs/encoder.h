// OKVS encoder/decoder using GF(2^128) and Gaussian elimination.

#pragma once

#include "okvs/gf128.h"
#include "okvs/row_hasher.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace okvs {

struct KeyView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct OkvsConfig {
    std::size_t weight = 3;
    std::size_t securityParameter = 40;
    double sparseExpansion = 1.23; // m' ≈ sparseExpansion * n
    std::size_t explicitSparseSize = 0;
    std::size_t explicitDenseSize = 0;
};

class OkvsEncoder {
public:
    OkvsEncoder(OkvsConfig config, std::uint64_t seed);

    struct EncodedTable {
        std::vector<GF128> data;
        std::size_t sparseColumns = 0;
        std::size_t denseColumns = 0;
        std::size_t gap = 0;
        std::size_t delta = 0;
    };

    struct EncodedTableView {
        std::span<const GF128> data;
        std::size_t sparseColumns = 0;
        std::size_t denseColumns = 0;
    };

    [[nodiscard]] EncodedTable encode(std::span<const KeyView> keys,
                                      std::span<const GF128> values) const;
    [[nodiscard]] GF128 decode(KeyView key, const EncodedTableView& table) const;
    void decode(std::span<const KeyView> keys,
                std::span<GF128> values,
                const EncodedTableView& table,
                std::size_t numThreads = 1) const;

    [[nodiscard]] std::size_t sparseSize(std::size_t numItems) const;
    [[nodiscard]] std::size_t denseSize(std::size_t numItems) const;

private:
    OkvsConfig mConfig;
    std::uint64_t mSeed;

    RowHasher makeHasher(std::size_t numItems) const;
};

} // namespace okvs
