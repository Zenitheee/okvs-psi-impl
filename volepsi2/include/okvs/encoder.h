// OKVS encoder/decoder using GF(2^128) and Gaussian elimination.

#pragma once

#include "okvs/gf128.h"
#include "okvs/row_hasher.h"

#include <cstddef>
#include <string>
#include <vector>

namespace okvs {

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

    [[nodiscard]] EncodedTable encode(const std::vector<std::string>& keys,
                                      const std::vector<GF128>& values) const;
    [[nodiscard]] GF128 decode(const std::string& key, const EncodedTable& table) const;

    [[nodiscard]] std::size_t sparseSize(std::size_t numItems) const;
    [[nodiscard]] std::size_t denseSize(std::size_t numItems) const;

private:
    OkvsConfig mConfig;
    std::uint64_t mSeed;

    RowHasher makeHasher(std::size_t numItems) const;
};

} // namespace okvs

