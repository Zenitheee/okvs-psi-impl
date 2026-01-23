#include "okvs/encoder.h"

#include <iostream>
#include <random>
#include <span>

int main() {
    using namespace okvs;

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
    return 0;
}

