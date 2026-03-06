#pragma once

#include "okvs/encoder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace okvs {

struct PsiConfig {
    OkvsConfig okvs;
    std::size_t binSizeHint = 1 << 14;
    std::size_t numThreads = 1;
    std::uint64_t seed = 0xc0ffee1234567890ULL;
};

struct PsiResult {
    std::vector<std::size_t> intersectionIndices;
    std::size_t okvsSize = 0;
    bool usedClustering = false;
};

class SemiHonestPsi {
public:
    explicit SemiHonestPsi(PsiConfig config = {});

    [[nodiscard]] PsiResult run(std::span<const KeyView> receiverSet,
                                std::span<const KeyView> senderSet) const;

private:
    PsiConfig mConfig;
};

} // namespace okvs
