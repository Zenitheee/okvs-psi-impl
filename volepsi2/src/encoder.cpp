#include "okvs/encoder.h"

#include "okvs/paxos.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace okvs {
namespace {

std::vector<RowData> buildRows(const RowHasher& hasher, std::span<const KeyView> keys) {
    std::vector<RowData> rows;
    rows.reserve(keys.size());
    for (const auto& key : keys) {
        if (key.size > 0 && key.data == nullptr) {
            throw std::runtime_error("KeyView points to null data.");
        }
        std::span<const std::uint8_t> keyBytes;
        if (key.size == 0 || key.data == nullptr) {
            keyBytes = {};
        } else {
            keyBytes = std::span<const std::uint8_t>(key.data, key.size);
        }
        rows.emplace_back(hasher.generate(keyBytes));
    }
    return rows;
}

GF128 decodeWithHasher(const RowHasher& hasher,
                       KeyView key,
                       std::span<const GF128> tableData,
                       std::size_t sparseColumns,
                       std::size_t denseColumns) {
    if (key.size > 0 && key.data == nullptr) {
        throw std::runtime_error("KeyView points to null data.");
    }

    std::span<const std::uint8_t> keyBytes;
    if (key.size == 0 || key.data == nullptr) {
        keyBytes = {};
    } else {
        keyBytes = std::span<const std::uint8_t>(key.data, key.size);
    }

    RowData row = hasher.generate(keyBytes);
    GF128 acc = GF128::zero();

    for (auto idx : row.sparse) {
        if (idx >= sparseColumns) {
            throw std::runtime_error("Sparse index out of range during decode.");
        }
        acc += tableData[idx];
    }

    for (std::size_t j = 0; j < row.dense.size(); ++j) {
        if (j >= denseColumns) {
            throw std::runtime_error("Dense index out of range during decode.");
        }
        acc += row.dense[j] * tableData[sparseColumns + j];
    }

    return acc;
}

void backfillMainColumns(const std::vector<RowData>& rows,
                         const std::vector<std::size_t>& mainRows,
                         const std::vector<std::size_t>& mainCols,
                         std::span<const GF128> values,
                         std::size_t sparseColumns,
                         std::size_t denseColumns,
                         std::vector<GF128>& encoded) {
    for (std::size_t idx = 0; idx < mainRows.size(); ++idx) {
        const auto rowIndex = mainRows[idx];
        const auto pivotCol = mainCols[idx];
        if (rowIndex >= values.size()) {
            throw std::runtime_error("Value index out of range during back substitution.");
        }
        GF128 acc = values[rowIndex];

        for (auto col : rows[rowIndex].sparse) {
            if (col >= sparseColumns) {
                throw std::runtime_error("Sparse column index out of range during back substitution.");
            }
            if (col == pivotCol) {
                continue;
            }
            if (col < encoded.size()) {
                acc -= encoded[col];
            }
        }

        const auto& denseRow = rows[rowIndex].dense;
        for (std::size_t j = 0; j < denseColumns && j < denseRow.size(); ++j) {
            const auto coeff = denseRow[j];
            if (coeff.isZero()) {
                continue;
            }
            const auto& denseVal = encoded[sparseColumns + j];
            if (!denseVal.isZero()) {
                acc -= coeff * denseVal;
            }
        }

        encoded[pivotCol] = acc;
    }
}

} // namespace

OkvsEncoder::OkvsEncoder(OkvsConfig config, std::uint64_t seed)
    : mConfig(config),
      mSeed(seed) {}

std::size_t OkvsEncoder::sparseSize(std::size_t numItems) const {
    if (mConfig.explicitSparseSize != 0) {
        return mConfig.explicitSparseSize;
    }
    if (numItems == 0) {
        return 0;
    }
    return std::max<std::size_t>(
        mConfig.weight,
        static_cast<std::size_t>(std::ceil(mConfig.sparseExpansion * static_cast<double>(numItems))));
}

std::size_t OkvsEncoder::denseSize(std::size_t numItems) const {
    if (mConfig.explicitDenseSize != 0) {
        return mConfig.explicitDenseSize;
    }
    return mConfig.securityParameter;
}

RowHasher OkvsEncoder::makeHasher(std::size_t numItems) const {
    return RowHasher(sparseSize(numItems), denseSize(numItems), mConfig.weight, mSeed);
}

OkvsEncoder::EncodedTable OkvsEncoder::encode(std::span<const KeyView> keys,
                                              std::span<const GF128> values) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/value vector sizes.");
    }

    const std::size_t n = keys.size();
    RowHasher hasher = makeHasher(n);
    const std::size_t sparse = hasher.sparseSize();
    const std::size_t dense = hasher.denseSize();
    const std::size_t totalColumns = sparse + dense;

    auto rows = buildRows(hasher, keys);
    auto tri = triangulateRows(rows, sparse);
    const std::size_t delta = tri.mainRows.size();
    const std::size_t gap = tri.gapRows.size();
    if (delta + gap != n) {
        throw std::runtime_error("Triangulation result inconsistent with input size.");
    }

    std::vector<GF128> encoded(totalColumns, GF128::zero());
    FCInverse fcInv;
    if (gap > 0) {
        if (dense == 0) {
            throw std::runtime_error("Triangulation produced a gap but dense columns are zero.");
        }
        fcInv = buildFCInverse(tri, rows, sparse);
        auto rhs = computeGapRightHandSide(tri, values, fcInv);
        std::vector<std::size_t> denseCols;
        std::vector<GF128> denseSolution(dense, GF128::zero());
        if (!solveDenseGapSystem(tri, rows, fcInv, dense, rhs, denseCols, denseSolution)) {
            throw std::runtime_error("Failed to solve GF128 dense gap system.");
        }
        for (std::size_t j = 0; j < dense; ++j) {
            encoded[sparse + j] = denseSolution[j];
        }
    }

    std::vector<std::size_t> mainRowsReversed = tri.mainRows;
    std::vector<std::size_t> mainColsReversed = tri.mainCols;
    std::reverse(mainRowsReversed.begin(), mainRowsReversed.end());
    std::reverse(mainColsReversed.begin(), mainColsReversed.end());
    backfillMainColumns(rows, mainRowsReversed, mainColsReversed, values, sparse, dense, encoded);

    EncodedTable table;
    table.data = std::move(encoded);
    table.sparseColumns = sparse;
    table.denseColumns = dense;
    table.gap = gap;
    table.delta = delta;
    return table;
}

GF128 OkvsEncoder::decode(KeyView key, const EncodedTableView& table) const {
    if (table.data.size() < table.sparseColumns + table.denseColumns) {
        throw std::runtime_error("Encoded table view smaller than expected.");
    }
    RowHasher hasher(table.sparseColumns, table.denseColumns, mConfig.weight, mSeed);
    return decodeWithHasher(hasher, key, table.data, table.sparseColumns, table.denseColumns);
}

void OkvsEncoder::decode(std::span<const KeyView> keys,
                         std::span<GF128> values,
                         const EncodedTableView& table,
                         std::size_t numThreads) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/output sizes for decode.");
    }
    if (table.data.size() < table.sparseColumns + table.denseColumns) {
        throw std::runtime_error("Encoded table view smaller than expected.");
    }

    RowHasher hasher(table.sparseColumns, table.denseColumns, mConfig.weight, mSeed);
    const std::size_t threads = std::max<std::size_t>(1, numThreads);

    std::atomic<std::size_t> next{0};
    std::exception_ptr workerError = nullptr;

    auto worker = [&]() {
        try {
            while (true) {
                const auto i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= keys.size()) {
                    break;
                }
                values[i] = decodeWithHasher(hasher, keys[i], table.data, table.sparseColumns, table.denseColumns);
            }
        } catch (...) {
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    };

    if (threads == 1 || keys.size() < 2) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) {
            pool.emplace_back(worker);
        }
        for (auto& thread : pool) {
            thread.join();
        }
    }

    if (workerError) {
        std::rethrow_exception(workerError);
    }
}

} // namespace okvs
