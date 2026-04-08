#include "okvs/binned_encoder.h"
#include "okvs/encoder.h"
#include "okvs/input_dataset.h"
#include "okvs/paxos.h"
#include "okvs/row_hasher.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

using okvs::BinnedOkvsEncoder;
using okvs::GF128;
using okvs::KeyView;
using okvs::OkvsConfig;
using okvs::OkvsEncoder;
using okvs::RowHasher;

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

bool runFieldSanity() {
    const auto zero = GF128::zero();
    if (!(zero.square() == zero) || !(zero.square() == (zero * zero))) {
        std::cerr << "GF128 zero square sanity failed\n";
        return false;
    }

    const auto one = GF128::one();
    if (!(one.square() == one) || !(one.inverse() == one)) {
        std::cerr << "GF128 one sanity failed\n";
        return false;
    }

    std::mt19937_64 rng(12345);
    for (std::size_t i = 0; i < 2048; ++i) {
        GF128 value(rng(), rng());
        if (!(value.square() == (value * value))) {
            std::cerr << "GF128 square mismatch\n";
            return false;
        }

        if (!value.isZero()) {
            if (!((value * value.inverse()) == GF128::one())) {
                std::cerr << "GF128 inverse mismatch\n";
                return false;
            }
        }
    }

    return true;
}

bool runOkvsRoundTripCase(std::size_t n) {
    OkvsConfig config;
    OkvsEncoder encoder(config, 0x5eed5eedULL);

    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) {
            keys.emplace_back();
        } else {
            keys.emplace_back("core-roundtrip-key-" + std::to_string(n) + "-" + std::to_string(i));
        }
    }

    const auto values = makeValues(n, 1337 + n);
    const auto keyViews = makeKeyViews(keys);

    okvs::OkvsEncoder::EncodedTable table;
    try {
        table = encoder.encode(
            std::span<const KeyView>(keyViews.data(), keyViews.size()),
            std::span<const GF128>(values.data(), values.size()));
    } catch (const std::exception& ex) {
        std::cerr << "OKVS encode failed for n=" << n << ": " << ex.what() << '\n';
        return false;
    }

    if (n > 0 && table.sparseColumns < config.weight) {
        std::cerr << "OKVS sparse table smaller than row weight\n";
        return false;
    }
    if (table.denseColumns != config.securityParameter) {
        std::cerr << "OKVS dense width should use the default security-parameter tail\n";
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

    if (!verifyDecoded("OKVS single-thread decode", singleThread, values)) {
        return false;
    }
    if (!verifyDecoded("OKVS multi-thread decode", multiThread, values)) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const auto recovered = encoder.decode(keyViews[i], tableView);
        if (!(recovered == values[i])) {
            std::cerr << "OKVS single-key decode mismatch at index " << i << '\n';
            return false;
        }
    }

    return true;
}

bool runEmptyOkvsCase() {
    OkvsConfig config;
    OkvsEncoder encoder(config, 0x9eed5eedULL);

    const std::vector<KeyView> emptyKeys;
    const std::vector<GF128> emptyValues;
    const auto table = encoder.encode(
        std::span<const KeyView>(emptyKeys.data(), emptyKeys.size()),
        std::span<const GF128>(emptyValues.data(), emptyValues.size()));

    if (table.sparseColumns != 0) {
        std::cerr << "empty OKVS encode should not allocate sparse columns\n";
        return false;
    }
    if (table.denseColumns != 0) {
        std::cerr << "empty OKVS encode dense column count mismatch\n";
        return false;
    }
    if (!table.data.empty()) {
        std::cerr << "empty OKVS encode table size mismatch\n";
        return false;
    }

    return true;
}

bool runBinnedRoundTripCase() {
    OkvsConfig config;
    constexpr std::size_t n = 96;
    constexpr std::size_t binSizeHint = 8;
    BinnedOkvsEncoder encoder(config, 0x42424242ULL, n, binSizeHint);

    if (encoder.numBins() <= 1) {
        std::cerr << "clustered OKVS should use multiple bins in the core test\n";
        return false;
    }
    if (encoder.densePerBin() != config.securityParameter) {
        std::cerr << "clustered OKVS dense width should use the default security-parameter tail\n";
        return false;
    }

    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) {
            keys.emplace_back();
        } else {
            keys.emplace_back("binned-core-key-" + std::to_string(i));
        }
    }

    const auto values = makeValues(n, 0x51515151ULL);
    const auto keyViews = makeKeyViews(keys);
    std::vector<GF128> table(encoder.totalSize(), GF128::zero());

    encoder.encode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<const GF128>(values.data(), values.size()),
        std::span<GF128>(table.data(), table.size()),
        4);

    std::vector<GF128> singleThread(values.size(), GF128::zero());
    std::vector<GF128> multiThread(values.size(), GF128::zero());
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(singleThread.data(), singleThread.size()),
        std::span<const GF128>(table.data(), table.size()),
        1);
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(multiThread.data(), multiThread.size()),
        std::span<const GF128>(table.data(), table.size()),
        4);

    if (!verifyDecoded("clustered OKVS single-thread decode", singleThread, values)) {
        return false;
    }
    if (!verifyDecoded("clustered OKVS multi-thread decode", multiThread, values)) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const auto recovered = encoder.decode(keyViews[i], std::span<const GF128>(table.data(), table.size()));
        if (!(recovered == values[i])) {
            std::cerr << "clustered OKVS single-key decode mismatch at index " << i << '\n';
            return false;
        }
    }

    std::vector<std::size_t> affectedClusters;
    affectedClusters.reserve(n);
    const auto totalSparse = encoder.numBins() * encoder.sparsePerBin();
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t changedClusters = 0;
        std::size_t selectedCluster = encoder.numBins();

        for (std::size_t cluster = 0; cluster < encoder.numBins(); ++cluster) {
            auto modifiedTable = table;
            const auto sparseStart = cluster * encoder.sparsePerBin();
            const auto denseStart = totalSparse + cluster * encoder.densePerBin();
            std::fill_n(modifiedTable.begin() + static_cast<std::ptrdiff_t>(sparseStart),
                        static_cast<std::ptrdiff_t>(encoder.sparsePerBin()),
                        GF128::zero());
            std::fill_n(modifiedTable.begin() + static_cast<std::ptrdiff_t>(denseStart),
                        static_cast<std::ptrdiff_t>(encoder.densePerBin()),
                        GF128::zero());

            const auto decoded = encoder.decode(
                keyViews[i],
                std::span<const GF128>(modifiedTable.data(), modifiedTable.size()));
            if (!(decoded == values[i])) {
                ++changedClusters;
                selectedCluster = cluster;
            }
        }

        if (changedClusters != 1) {
            std::cerr << "clustered OKVS row should depend on exactly one cluster, key index "
                      << i << " changed clusters=" << changedClusters << '\n';
            return false;
        }
        affectedClusters.push_back(selectedCluster);
    }

    if (std::adjacent_find(affectedClusters.begin(), affectedClusters.end(),
                           [](std::size_t lhs, std::size_t rhs) { return lhs != rhs; }) ==
        affectedClusters.end()) {
        std::cerr << "clustered OKVS test did not exercise multiple clusters\n";
        return false;
    }

    return true;
}

bool runInputDatasetPreprocessingCase() {
    const auto prepared = okvs::preparePsiDataset(
        " alice \n bob\nalice\n\ncarol, dave ; eve\tbob ",
        "eve\n mallory\n bob \n eve \n");

    const std::vector<std::string> expectedReceiver = {
        "alice",
        "bob",
        "carol",
        "dave",
        "eve"
    };
    const std::vector<std::string> expectedSender = {
        "eve",
        "mallory",
        "bob"
    };

    if (prepared.receiverItems != expectedReceiver) {
        std::cerr << "receiver preprocessing mismatch\n";
        return false;
    }
    if (prepared.senderItems != expectedSender) {
        std::cerr << "sender preprocessing mismatch\n";
        return false;
    }
    if (prepared.receiverRawCount != 7 || prepared.senderRawCount != 4) {
        std::cerr << "raw item counting mismatch\n";
        return false;
    }
    if (prepared.receiverDuplicateCount != 2 || prepared.senderDuplicateCount != 1) {
        std::cerr << "duplicate counting mismatch\n";
        return false;
    }
    if (prepared.intersectionSize != 2) {
        std::cerr << "intersection counting mismatch\n";
        return false;
    }

    return true;
}

bool runRowHasherSparseSamplingCase() {
    constexpr std::size_t sparseColumns = 17;
    constexpr std::size_t denseColumns = 8;
    constexpr std::size_t weight = 3;
    RowHasher hasher(sparseColumns, denseColumns, weight, 0x1122334455667788ULL);

    for (std::size_t i = 0; i < 2048; ++i) {
        const auto key = "row-hasher-key-" + std::to_string(i);
        const auto keyBytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(key.data()),
            key.size());
        const auto row = hasher.generate(keyBytes);

        if (row.sparse.size() != weight) {
            std::cerr << "row hasher sparse weight mismatch\n";
            return false;
        }
        if (row.dense.size() != denseColumns) {
            std::cerr << "row hasher dense width mismatch\n";
            return false;
        }
        if (!std::is_sorted(row.sparse.begin(), row.sparse.end())) {
            std::cerr << "row hasher sparse indices should be sorted\n";
            return false;
        }
        if (std::adjacent_find(row.sparse.begin(), row.sparse.end()) != row.sparse.end()) {
            std::cerr << "row hasher sparse indices should be unique\n";
            return false;
        }
        for (const auto col : row.sparse) {
            if (col >= sparseColumns) {
                std::cerr << "row hasher sparse index out of range\n";
                return false;
            }
        }
    }

    RowHasher nearFullHasher(5, 0, 5, 0x8877665544332211ULL);
    for (std::size_t i = 0; i < 128; ++i) {
        const auto key = "row-hasher-full-domain-" + std::to_string(i);
        const auto keyBytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(key.data()),
            key.size());
        const auto row = nearFullHasher.generate(keyBytes);
        if (row.sparse.size() != 5) {
            std::cerr << "row hasher full-domain weight mismatch\n";
            return false;
        }
        for (std::size_t col = 0; col < row.sparse.size(); ++col) {
            if (row.sparse[col] != col) {
                std::cerr << "row hasher full-domain sample should cover every sparse column\n";
                return false;
            }
        }
    }

    return true;
}

bool runExactDenseGapSolveCase() {
    okvs::TriangulationInfo tri;
    tri.gapRows = {
        okvs::GapRowInfo{0, 0},
        okvs::GapRowInfo{1, 1}
    };

    std::vector<okvs::RowData> rows(2);
    rows[0].dense = {
        GF128::one(),
        GF128::zero(),
        GF128::one()
    };
    rows[1].dense = {
        GF128::zero(),
        GF128::zero(),
        GF128::one()
    };

    okvs::FCInverse fcInv;
    fcInv.dependencies.resize(2);

    const std::vector<GF128> rhs = {
        GF128(0, 5),
        GF128(0, 7)
    };
    std::vector<std::size_t> chosenDenseCols;
    std::vector<GF128> denseSolution(3, GF128::zero());
    if (!okvs::solveDenseGapSystem(tri, rows, fcInv, 3, rhs, chosenDenseCols, denseSolution)) {
        std::cerr << "exact dense gap solver should find a full-rank column set\n";
        return false;
    }

    const std::vector<std::size_t> expectedCols = {0, 2};
    if (chosenDenseCols != expectedCols) {
        std::cerr << "exact dense gap solver selected unexpected pivot columns\n";
        return false;
    }

    const GF128 eq0 =
        rows[0].dense[0] * denseSolution[0] +
        rows[0].dense[1] * denseSolution[1] +
        rows[0].dense[2] * denseSolution[2];
    const GF128 eq1 =
        rows[1].dense[0] * denseSolution[0] +
        rows[1].dense[1] * denseSolution[1] +
        rows[1].dense[2] * denseSolution[2];
    if (!(eq0 == rhs[0]) || !(eq1 == rhs[1])) {
        std::cerr << "exact dense gap solver returned an invalid solution\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runFieldSanity()) {
        return 1;
    }

    const std::vector<std::size_t> sizes = {1, 2, 4, 8, 64, 256};
    for (const auto n : sizes) {
        if (!runOkvsRoundTripCase(n)) {
            return 1;
        }
    }

    if (!runEmptyOkvsCase()) {
        return 1;
    }
    if (!runBinnedRoundTripCase()) {
        return 1;
    }
    if (!runInputDatasetPreprocessingCase()) {
        return 1;
    }
    if (!runRowHasherSparseSamplingCase()) {
        return 1;
    }
    if (!runExactDenseGapSolveCase()) {
        return 1;
    }

    std::cout << "Core backend tests passed.\n";
    return 0;
}
