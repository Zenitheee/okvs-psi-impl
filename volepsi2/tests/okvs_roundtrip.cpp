#include "okvs/encoder.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using okvs::GF128;
using okvs::KeyView;
using okvs::OkvsConfig;
using okvs::OkvsEncoder;

std::vector<KeyView> makeKeyViews(const std::vector<std::string>& keys) {
    std::vector<KeyView> views(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto* data = keys[i].empty() ? nullptr : keys[i].data();
        views[i] = KeyView{
            reinterpret_cast<const std::uint8_t*>(data),
            keys[i].size()
        };
    }
    return views;
}

std::vector<GF128> makeValues(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<GF128> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.emplace_back(rng(), rng());
    }
    return values;
}

template <typename Fn>
bool expectRuntimeError(const char* label, Fn&& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    } catch (const std::exception& ex) {
        std::cerr << label << " threw an unexpected exception: " << ex.what() << '\n';
        return false;
    }

    std::cerr << label << " did not throw\n";
    return false;
}

bool verifyDecoded(const char* label,
                   std::span<const GF128> decoded,
                   std::span<const GF128> expected) {
    if (decoded.size() != expected.size()) {
        std::cerr << label << " size mismatch\n";
        return false;
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!(decoded[i] == expected[i])) {
            std::cerr << label << " mismatch at index " << i << '\n';
            return false;
        }
    }

    return true;
}

bool runRoundTripCase(std::size_t n) {
    OkvsConfig config;
    OkvsEncoder encoder(config, 0x5eed5eedULL);

    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) {
            keys.emplace_back();
        } else {
            keys.emplace_back("roundtrip-key-with-long-prefix-" + std::to_string(n) + "-" + std::to_string(i));
        }
    }
    const auto values = makeValues(n, 1337 + n);
    auto keyViews = makeKeyViews(keys);

    okvs::OkvsEncoder::EncodedTable table;
    try {
        table = encoder.encode(
            std::span<const KeyView>(keyViews.data(), keyViews.size()),
            std::span<const GF128>(values.data(), values.size()));
    } catch (const std::exception& ex) {
        std::cerr << "encode failed for n=" << n << " with error: " << ex.what() << '\n';
        return false;
    }

    if (n > 0 && table.sparseColumns < config.weight) {
        std::cerr << "sparse table for n=" << n << " is smaller than the row weight\n";
        return false;
    }

    OkvsEncoder::EncodedTableView tableView{
        std::span<const GF128>(table.data.data(), table.data.size()),
        table.sparseColumns,
        table.denseColumns
    };

    std::vector<GF128> singleThread(values.size(), GF128::zero());
    std::vector<GF128> multiThread(values.size(), GF128::zero());
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(singleThread.data(), singleThread.size()),
        tableView,
        1);
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(multiThread.data(), multiThread.size()),
        tableView,
        4);

    if (!verifyDecoded("single-thread decode", singleThread, values)) {
        return false;
    }
    if (!verifyDecoded("multi-thread decode", multiThread, values)) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        auto recovered = encoder.decode(keyViews[i], tableView);
        if (!(recovered == values[i])) {
            std::cerr << "single-key decode mismatch at n=" << n << " index " << i << '\n';
            return false;
        }
    }

    return true;
}

bool runEmptyCase() {
    OkvsConfig config;
    OkvsEncoder encoder(config, 0x9eed5eedULL);

    const std::vector<KeyView> emptyKeys;
    const std::vector<GF128> emptyValues;
    const auto table = encoder.encode(
        std::span<const KeyView>(emptyKeys.data(), emptyKeys.size()),
        std::span<const GF128>(emptyValues.data(), emptyValues.size()));

    if (table.sparseColumns != 0) {
        std::cerr << "empty encode should not allocate sparse columns\n";
        return false;
    }
    if (table.denseColumns != config.securityParameter) {
        std::cerr << "empty encode dense column count mismatch\n";
        return false;
    }
    if (table.data.size() != table.denseColumns) {
        std::cerr << "empty encode table size mismatch\n";
        return false;
    }
    if (table.gap != 0 || table.delta != 0) {
        std::cerr << "empty encode should not produce gap or main rows\n";
        return false;
    }

    return true;
}

bool runValidationCases() {
    OkvsConfig config;
    OkvsEncoder encoder(config, 0x12341234ULL);

    const std::vector<KeyView> invalidKeys = {
        KeyView{nullptr, 4}
    };
    const std::vector<GF128> invalidValues = {
        GF128::one()
    };
    if (!expectRuntimeError("encode null key", [&] {
            const auto ignored = encoder.encode(
                std::span<const KeyView>(invalidKeys.data(), invalidKeys.size()),
                std::span<const GF128>(invalidValues.data(), invalidValues.size()));
            (void)ignored;
        })) {
        return false;
    }

    const std::vector<std::string> validKeys = {"valid-key"};
    const auto validViews = makeKeyViews(validKeys);
    const auto validTable = encoder.encode(
        std::span<const KeyView>(validViews.data(), validViews.size()),
        std::span<const GF128>(invalidValues.data(), invalidValues.size()));
    OkvsEncoder::EncodedTableView tableView{
        std::span<const GF128>(validTable.data.data(), validTable.data.size()),
        validTable.sparseColumns,
        validTable.denseColumns
    };
    if (!expectRuntimeError("decode null key", [&] {
            const auto ignored = encoder.decode(KeyView{nullptr, 4}, tableView);
            (void)ignored;
        })) {
        return false;
    }

    OkvsConfig invalidConfig = config;
    invalidConfig.explicitSparseSize = 2;
    OkvsEncoder invalidEncoder(invalidConfig, 0xbeefbeefULL);
    if (!expectRuntimeError("invalid sparse size", [&] {
            const auto ignored = invalidEncoder.encode(
                std::span<const KeyView>(validViews.data(), validViews.size()),
                std::span<const GF128>(invalidValues.data(), invalidValues.size()));
            (void)ignored;
        })) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    const std::vector<std::size_t> sizes = {1, 2, 3, 4, 8, 64, 256};
    for (const auto n : sizes) {
        if (!runRoundTripCase(n)) {
            return 1;
        }
    }

    if (!runEmptyCase()) {
        return 1;
    }
    if (!runValidationCases()) {
        return 1;
    }

    std::cout << "OKVS correctness tests passed.\n";
    return 0;
}
