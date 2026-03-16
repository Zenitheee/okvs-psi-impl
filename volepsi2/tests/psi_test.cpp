#include "okvs/psi.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

constexpr bool kExpectRealVole = OKVS_ENABLE_REAL_VOLE != 0;
constexpr std::array<std::string_view, 5> kExpectedStageIds = {
    "hash_mapping",
    "okvs_encoding",
    "vole_generation",
    "correlation_transfer",
    "intersection_calculation"
};

std::vector<okvs::KeyView> makeKeyViews(const std::vector<std::string>& keys) {
    std::vector<okvs::KeyView> views(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        views[i] = okvs::KeyView{
            reinterpret_cast<const std::uint8_t*>(keys[i].data()),
            keys[i].size()
        };
    }
    return views;
}

std::vector<std::string> makeKeys(const std::string& prefix, std::size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.emplace_back(prefix + "-very-long-item-name-" + std::to_string(i));
    }
    return keys;
}

std::vector<std::size_t> expectedIntersection(const std::vector<std::string>& receiver,
                                              const std::vector<std::string>& sender) {
    std::unordered_set<std::string> senderSet(sender.begin(), sender.end());
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < receiver.size(); ++i) {
        if (senderSet.contains(receiver[i])) {
            indices.push_back(i);
        }
    }
    return indices;
}

bool validateTelemetry(const char* label,
                       const okvs::PsiResult& result,
                       std::size_t receiverSize,
                       std::size_t senderSize,
                       const std::vector<okvs::PsiTelemetry>& updates,
                       bool expectEstimatedTransfers) {
    const auto& telemetry = result.telemetry;
    if (telemetry.receiverSetSize != receiverSize || telemetry.senderSetSize != senderSize) {
        std::cerr << label << " failed: telemetry set sizes do not match inputs\n";
        return false;
    }

    if (receiverSize == 0) {
        if (!telemetry.stages.empty()) {
            std::cerr << label << " failed: empty receiver should not emit stage telemetry\n";
            return false;
        }
        if (!updates.empty()) {
            std::cerr << label << " failed: empty receiver should not invoke progress callback\n";
            return false;
        }
        if (telemetry.totalNetworkBytes != 0) {
            std::cerr << label << " failed: empty receiver should not report network bytes\n";
            return false;
        }
        return true;
    }

    if (telemetry.stages.size() != kExpectedStageIds.size()) {
        std::cerr << label << " failed: unexpected telemetry stage count\n";
        return false;
    }

    if (updates.size() != telemetry.stages.size()) {
        std::cerr << label << " failed: progress callback count does not match stage count\n";
        return false;
    }

    std::size_t stageByteSum = 0;
    for (std::size_t i = 0; i < telemetry.stages.size(); ++i) {
        const auto& stage = telemetry.stages[i];
        if (stage.id != kExpectedStageIds[i]) {
            std::cerr << label << " failed: unexpected telemetry stage order\n";
            return false;
        }
        if (stage.durationMs < 0.0) {
            std::cerr << label << " failed: negative stage duration\n";
            return false;
        }
        stageByteSum += stage.networkBytes;
    }

    if (telemetry.okvsSize != result.okvsSize ||
        telemetry.intersectionSize != result.intersectionIndices.size() ||
        telemetry.usedClustering != result.usedClustering ||
        telemetry.usedRealVole != result.usedRealVole) {
        std::cerr << label << " failed: telemetry summary does not match result\n";
        return false;
    }

    if (telemetry.totalNetworkBytes != stageByteSum) {
        std::cerr << label << " failed: telemetry total network bytes mismatch\n";
        return false;
    }

    if (telemetry.stages[0].networkBytes != 0 || telemetry.stages[1].networkBytes != 0) {
        std::cerr << label << " failed: hash/OKVS stages should not report transport bytes\n";
        return false;
    }

    if (telemetry.stages[3].networkBytes != result.okvsSize * sizeof(okvs::GF128)) {
        std::cerr << label << " failed: correction transfer bytes mismatch\n";
        return false;
    }

    if (telemetry.stages[3].networkBytesEstimated != expectEstimatedTransfers ||
        telemetry.stages[4].networkBytesEstimated != expectEstimatedTransfers) {
        std::cerr << label << " failed: unexpected transfer measurement mode\n";
        return false;
    }

    if (telemetry.stages[4].networkBytes != senderSize * sizeof(okvs::GF128)) {
        std::cerr << label << " failed: intersection payload bytes mismatch\n";
        return false;
    }

    if (result.usedRealVole && telemetry.stages[2].networkBytes == 0) {
        std::cerr << label << " failed: real VOLE stage reported zero traffic\n";
        return false;
    }

    if (updates.back().stages.size() != telemetry.stages.size() ||
        updates.back().totalNetworkBytes != telemetry.totalNetworkBytes ||
        updates.back().intersectionSize != result.intersectionIndices.size()) {
        std::cerr << label << " failed: final progress update does not match result telemetry\n";
        return false;
    }

    if (telemetry.totalDurationMs < updates.back().totalDurationMs) {
        std::cerr << label << " failed: final telemetry duration regressed\n";
        return false;
    }

    return true;
}

bool runCase(const char* label,
             const std::vector<std::string>& receiver,
             const std::vector<std::string>& sender,
             okvs::PsiConfig config,
             bool expectClustering,
             bool useLocalSocketFlow = false) {
    okvs::SemiHonestPsi psi(config);
    auto receiverViews = makeKeyViews(receiver);
    auto senderViews = makeKeyViews(sender);

    std::vector<okvs::PsiTelemetry> updates;
    const auto result = useLocalSocketFlow
        ? psi.runTwoPartyLocal(
            std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
            std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()),
            [&](const okvs::PsiTelemetry& telemetry) {
                updates.push_back(telemetry);
            })
        : psi.run(
            std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
            std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()),
            [&](const okvs::PsiTelemetry& telemetry) {
                updates.push_back(telemetry);
            });

    const auto expected = expectedIntersection(receiver, sender);
    if (result.intersectionIndices != expected) {
        std::cerr << label << " failed: intersection mismatch\n";
        return false;
    }

    if (result.usedClustering != expectClustering) {
        std::cerr << label << " failed: unexpected clustering flag\n";
        return false;
    }

    if (receiver.empty() && result.okvsSize != 0) {
        std::cerr << label << " failed: empty receiver should not build an OKVS\n";
        return false;
    }

    if (!receiver.empty() && result.usedRealVole != kExpectRealVole) {
        std::cerr << label << " failed: unexpected VOLE backend selection\n";
        return false;
    }

    if (!receiver.empty() && result.okvsSize == 0) {
        std::cerr << label << " failed: non-empty receiver produced zero-sized OKVS\n";
        return false;
    }

    if (!validateTelemetry(
            label,
            result,
            receiver.size(),
            sender.size(),
            updates,
            !useLocalSocketFlow)) {
        return false;
    }

    return true;
}

bool runThreadParityCase(const char* label,
                         const std::vector<std::string>& receiver,
                         const std::vector<std::string>& sender,
                         okvs::PsiConfig config) {
    auto receiverViews = makeKeyViews(receiver);
    auto senderViews = makeKeyViews(sender);

    okvs::PsiConfig singleThreadConfig = config;
    singleThreadConfig.numThreads = 1;
    okvs::SemiHonestPsi singleThreadPsi(singleThreadConfig);
    const auto singleThreadResult = singleThreadPsi.run(
        std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
        std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()));

    okvs::PsiConfig multiThreadConfig = config;
    multiThreadConfig.numThreads = 4;
    okvs::SemiHonestPsi multiThreadPsi(multiThreadConfig);
    const auto multiThreadResult = multiThreadPsi.run(
        std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
        std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()));

    if (singleThreadResult.intersectionIndices != multiThreadResult.intersectionIndices) {
        std::cerr << label << " failed: thread-count changed the intersection\n";
        return false;
    }

    if (singleThreadResult.okvsSize != multiThreadResult.okvsSize) {
        std::cerr << label << " failed: thread-count changed the OKVS size\n";
        return false;
    }

    if (singleThreadResult.usedClustering != multiThreadResult.usedClustering) {
        std::cerr << label << " failed: thread-count changed the clustering decision\n";
        return false;
    }

    if (singleThreadResult.usedRealVole != multiThreadResult.usedRealVole) {
        std::cerr << label << " failed: thread-count changed the VOLE backend\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    okvs::PsiConfig config;
    config.seed = 0x123456789abcdef0ULL;

    const std::vector<std::string> emptyReceiver;
    const auto senderOnly = makeKeys("sender-only", 8);
    if (!runCase("receiver_empty", emptyReceiver, senderOnly, config, false)) {
        return 1;
    }

    const auto receiverOnly = makeKeys("receiver-only", 9);
    const std::vector<std::string> emptySender;
    if (!runCase("sender_empty", receiverOnly, emptySender, config, false)) {
        return 1;
    }

    const auto noIntersectionReceiver = makeKeys("recv-none", 10);
    const auto noIntersectionSender = makeKeys("send-none", 7);
    if (!runCase("no_intersection", noIntersectionReceiver, noIntersectionSender, config, false)) {
        return 1;
    }

    const auto fullIntersection = makeKeys("full-match", 16);
    if (!runCase("full_intersection", fullIntersection, fullIntersection, config, false)) {
        return 1;
    }

    auto unevenReceiver = makeKeys("uneven-recv", 23);
    auto unevenSender = makeKeys("uneven-send", 11);
    unevenSender[1] = unevenReceiver[5];
    unevenSender[4] = unevenReceiver[9];
    unevenSender[9] = unevenReceiver[22];
    if (!runCase("partial_uneven", unevenReceiver, unevenSender, config, false)) {
        return 1;
    }

    okvs::PsiConfig localSocketConfig = config;
    localSocketConfig.numThreads = 2;
    auto localSocketReceiver = makeKeys("local-recv", 20);
    auto localSocketSender = makeKeys("local-send", 14);
    localSocketSender[0] = localSocketReceiver[3];
    localSocketSender[5] = localSocketReceiver[11];
    localSocketSender[9] = localSocketReceiver[18];
    if (!runCase("local_socket_flow", localSocketReceiver, localSocketSender, localSocketConfig, false, true)) {
        return 1;
    }

    okvs::PsiConfig thresholdConfig = config;
    thresholdConfig.binSizeHint = 8;
    auto thresholdReceiver = makeKeys("threshold-recv", 8);
    auto thresholdSender = makeKeys("threshold-send", 6);
    thresholdSender[2] = thresholdReceiver[5];
    if (!runCase("cluster_threshold_exact", thresholdReceiver, thresholdSender, thresholdConfig, false)) {
        return 1;
    }

    auto aboveThresholdReceiver = makeKeys("threshold-recv-plus", 9);
    auto aboveThresholdSender = makeKeys("threshold-send-plus", 6);
    aboveThresholdSender[3] = aboveThresholdReceiver[7];
    if (!runCase("cluster_threshold_above", aboveThresholdReceiver, aboveThresholdSender, thresholdConfig, true)) {
        return 1;
    }

    okvs::PsiConfig clusteredConfig = config;
    clusteredConfig.binSizeHint = 8;
    clusteredConfig.numThreads = 4;
    auto clusteredReceiver = makeKeys("cluster-recv", 64);
    auto clusteredSender = makeKeys("cluster-send", 48);
    for (std::size_t i = 0; i < clusteredSender.size(); i += 6) {
        clusteredSender[i] = clusteredReceiver[(i * 5) % clusteredReceiver.size()];
    }
    if (!runCase("clustered_multithread", clusteredReceiver, clusteredSender, clusteredConfig, true)) {
        return 1;
    }
    if (!runThreadParityCase("clustered_thread_parity", clusteredReceiver, clusteredSender, clusteredConfig)) {
        return 1;
    }

    std::cout << "PSI tests passed.\n";
    return 0;
}
