#include "okvs/psi.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

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

bool runCase(const char* label,
             const std::vector<std::string>& receiver,
             const std::vector<std::string>& sender,
             okvs::PsiConfig config,
             bool expectClustering) {
    okvs::SemiHonestPsi psi(config);
    auto receiverViews = makeKeyViews(receiver);
    auto senderViews = makeKeyViews(sender);

    const auto result = psi.run(
        std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
        std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()));

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

    if (!receiver.empty() && result.okvsSize == 0) {
        std::cerr << label << " failed: non-empty receiver produced zero-sized OKVS\n";
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

    std::cout << "PSI tests passed.\n";
    return 0;
}
