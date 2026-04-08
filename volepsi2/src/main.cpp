#include "okvs/encoder.h"
#include "okvs/psi.h"

#include <iostream>
#include <random>
#include <span>
#include <string>
#include <vector>

int main() {
    using namespace okvs;

    auto makeViews = [](const std::vector<std::string>& items) {
        std::vector<KeyView> views(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            views[i] = KeyView{
                reinterpret_cast<const std::uint8_t*>(items[i].data()),
                items[i].size()
            };
        }
        return views;
    };

    OkvsConfig config;
    OkvsEncoder encoder(config, 0x1337'1145ULL);

    constexpr std::size_t n = 32;
    std::vector<std::string> keys;
    std::vector<GF128> values;
    keys.reserve(n);
    values.reserve(n);

    std::mt19937_64 rng(42);
    for (std::size_t i = 0; i < n; ++i) {
        keys.emplace_back("key-" + std::to_string(i));
        uint64_t lo = rng();
        uint64_t hi = rng();
        values.emplace_back(GF128(hi, lo));
    }

    std::vector<KeyView> keyViews(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        const auto* data = key.empty() ? nullptr : key.data();
        keyViews[i] = KeyView{
            reinterpret_cast<const std::uint8_t*>(data),
            key.size()
        };
    }

    auto table = encoder.encode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<const GF128>(values.data(), values.size()));

    OkvsEncoder::EncodedTableView tableView{
        std::span<const GF128>(table.data),
        table.sparseColumns,
        table.denseColumns
    };

    bool ok = true;
    for (std::size_t i = 0; i < n; ++i) {
        auto recovered = encoder.decode(keyViews[i], tableView);
        if (!(recovered == values[i])) {
            ok = false;
            std::cerr << "Mismatch at index " << i << " expected " << values[i]
                      << " got " << recovered << '\n';
        }
    }

    if (!ok) {
        std::cerr << "Verification failed.\n";
        return 1;
    }

    std::cout << "OKVS encode/decode verified for n=" << n << ".\n";
    std::cout << "Sparse columns: " << table.sparseColumns
              << ", dense columns: " << table.denseColumns << '\n';

    std::vector<std::string> receiverItems = {
        "receiver-item-000000",
        "receiver-item-000001",
        "receiver-item-000002",
        "receiver-item-000003"
    };
    std::vector<std::string> senderItems = {
        "sender-item-000000",
        "receiver-item-000001",
        "sender-item-000002",
        "receiver-item-000003"
    };

    PsiConfig psiConfig;
    psiConfig.deterministicSeedEnabled = true;
    psiConfig.seed = 0xabcdef1234567890ULL;
    SemiHonestPsi psi(psiConfig);

    auto receiverViews = makeViews(receiverItems);
    auto senderViews = makeViews(senderItems);
    const auto psiResult = psi.run(
        std::span<const KeyView>(receiverViews.data(), receiverViews.size()),
        std::span<const KeyView>(senderViews.data(), senderViews.size()));

    std::cout << "PSI intersection indices:";
    for (auto idx : psiResult.intersectionIndices) {
        std::cout << ' ' << idx;
    }
    std::cout << "\nPSI VOLE backend: "
              << (psiResult.usedRealVole ? "real" : "simulated");
    std::cout << "\nPSI transfer mode: "
              << (psiResult.usedModeledTransfers
                      ? "benchmark-style fallback (modeled correction/tag transfers)"
                      : "local socket demo (measured correction/tag transfers)");
    std::cout << "\nPSI transport bytes: measured=" << psiResult.telemetry.totalNetworkBytes
              << ", modeled=" << psiResult.telemetry.totalEstimatedNetworkBytes;
    std::cout << "\nPSI intersection values:";
    for (auto idx : psiResult.intersectionIndices) {
        std::cout << ' ' << receiverItems[idx];
    }
    std::cout << '\n';
    return 0;
}
