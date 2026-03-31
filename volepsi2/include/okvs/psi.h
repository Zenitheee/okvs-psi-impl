#pragma once

#include "okvs/encoder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace okvs {

struct PsiConfig {
    OkvsConfig okvs;
    std::size_t binSizeHint = 1 << 14;
    std::size_t numThreads = 1;
    std::uint64_t seed = 0xc0ffee1234567890ULL;
};

struct PsiStageStat {
    std::string id;
    std::string label;
    std::string detail;
    double durationMs = 0.0;
    std::size_t networkBytes = 0;
    bool networkBytesEstimated = false;
};

struct PsiTelemetry {
    std::vector<PsiStageStat> stages;
    double totalDurationMs = 0.0;
    std::size_t totalNetworkBytes = 0;
    std::size_t receiverSetSize = 0;
    std::size_t senderSetSize = 0;
    std::size_t okvsSize = 0;
    std::size_t intersectionSize = 0;
    bool usedClustering = false;
    bool usedRealVole = false;
};

using PsiTelemetryCallback = std::function<void(const PsiTelemetry&)>;

struct PsiResult {
    // Indices of the first matching occurrences in the original receiver input.
    std::vector<std::size_t> intersectionIndices;
    std::size_t okvsSize = 0;
    bool usedClustering = false;
    bool usedRealVole = false;
    PsiTelemetry telemetry;
};

class SemiHonestPsi {
public:
    explicit SemiHonestPsi(PsiConfig config = {});

    [[nodiscard]] PsiResult run(std::span<const KeyView> receiverSet,
                                std::span<const KeyView> senderSet) const;
    [[nodiscard]] PsiResult run(std::span<const KeyView> receiverSet,
                                std::span<const KeyView> senderSet,
                                const PsiTelemetryCallback& onUpdate) const;
    [[nodiscard]] PsiResult runTwoPartyLocal(std::span<const KeyView> receiverSet,
                                             std::span<const KeyView> senderSet) const;
    [[nodiscard]] PsiResult runTwoPartyLocal(std::span<const KeyView> receiverSet,
                                             std::span<const KeyView> senderSet,
                                             const PsiTelemetryCallback& onUpdate) const;

private:
    PsiConfig mConfig;
};

} // namespace okvs
