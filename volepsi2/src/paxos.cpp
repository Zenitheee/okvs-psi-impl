#include "okvs/paxos.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

namespace okvs {
namespace {

constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

bool solveFullRowRankSystem(std::vector<std::vector<GF128>>& matrix,
                            std::vector<GF128>& rhs,
                            std::vector<std::size_t>& pivotColumns) {
    const std::size_t rows = matrix.size();
    if (rows == 0) {
        pivotColumns.clear();
        return true;
    }
    const std::size_t cols = matrix.front().size();
    pivotColumns.clear();
    pivotColumns.reserve(rows);

    std::size_t pivotRow = 0;
    for (std::size_t col = 0; col < cols && pivotRow < rows; ++col) {
        std::size_t candidate = pivotRow;
        while (candidate < rows && matrix[candidate][col].isZero()) {
            ++candidate;
        }

        if (candidate == rows) {
            continue;
        }

        if (candidate != pivotRow) {
            std::swap(matrix[candidate], matrix[pivotRow]);
            std::swap(rhs[candidate], rhs[pivotRow]);
        }

        GF128 diag = matrix[pivotRow][col];
        if (diag.isZero()) {
            return false;
        }

        const GF128 inv = diag.inverse();
        if (!(inv == GF128::one())) {
            for (std::size_t j = col; j < cols; ++j) {
                matrix[pivotRow][j] *= inv;
            }
            rhs[pivotRow] *= inv;
        }

        for (std::size_t row = 0; row < rows; ++row) {
            if (row == pivotRow) {
                continue;
            }
            const GF128 factor = matrix[row][col];
            if (factor.isZero()) {
                continue;
            }
            for (std::size_t j = col; j < cols; ++j) {
                matrix[row][j] -= factor * matrix[pivotRow][j];
            }
            rhs[row] -= factor * rhs[pivotRow];
        }

        pivotColumns.push_back(col);
        ++pivotRow;
    }

    return pivotRow == rows;
}

std::vector<std::vector<GF128>> buildCombinedDense(const TriangulationInfo& tri,
                                                   const std::vector<RowData>& rows,
                                                   const FCInverse& fcInv,
                                                   std::size_t denseColumns) {
    const std::size_t g = tri.gapRows.size();
    std::vector<std::vector<GF128>> combined(g, std::vector<GF128>(denseColumns, GF128::zero()));

    for (std::size_t i = 0; i < g; ++i) {
        const auto gapRowIdx = tri.gapRows[i].row;
        const auto& base = rows[gapRowIdx].dense;
        if (base.size() < denseColumns) {
            throw std::runtime_error("Dense row shorter than expected.");
        }
        combined[i] = base;

        for (auto helperRow : fcInv.dependencies[i]) {
            const auto& helperDense = rows[helperRow].dense;
            if (helperDense.size() < denseColumns) {
                throw std::runtime_error("Helper dense row shorter than expected.");
            }
            for (std::size_t j = 0; j < denseColumns; ++j) {
                combined[i][j] += helperDense[j];
            }
        }
    }

    return combined;
}

} // namespace

TriangulationInfo triangulateRows(const std::vector<RowData>& rows, std::size_t sparseColumns) {
    TriangulationInfo result;
    const std::size_t n = rows.size();
    if (n == 0 || sparseColumns == 0) {
        return result;
    }

    // Optimization: Use flat arrays instead of vector<vector> for columnToRows
    // 1. Count weights
    std::vector<std::size_t> colWeights(sparseColumns, 0);
    for (const auto& row : rows) {
        for (auto col : row.sparse) {
             if (col < sparseColumns) {
                 colWeights[col]++;
             }
        }
    }

    // 2. Compute offsets (CSR-like structure)
    std::vector<std::size_t> colOffsets(sparseColumns + 1, 0);
    std::size_t totalEntries = 0;
    for (std::size_t i = 0; i < sparseColumns; ++i) {
        colOffsets[i] = totalEntries;
        totalEntries += colWeights[i];
    }
    colOffsets[sparseColumns] = totalEntries;

    // 3. Fill the flat array
    std::vector<std::size_t> colData(totalEntries);
    std::vector<std::size_t> currentPos = colOffsets; // specific cursor for each col
    
    for (std::size_t r = 0; r < n; ++r) {
        for (auto col : rows[r].sparse) {
            if (col < sparseColumns) {
                colData[currentPos[col]++] = r;
            } else {
                 throw std::runtime_error("Sparse column index out of range.");
            }
        }
    }

    // Access helper:
    auto getRowsForCol = [&](std::size_t col) -> std::span<const std::size_t> {
        return {&colData[colOffsets[col]], colWeights[col]}; // currentPos[col] should equal colOffsets[col+1] at end
    };
    
    std::vector<std::size_t> currentWeight = colWeights; // Copy for mutation
    using HeapEntry = std::pair<std::size_t, std::size_t>;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> heap;

    for (std::size_t col = 0; col < sparseColumns; ++col) {
        if (currentWeight[col] != 0) {
            heap.emplace(currentWeight[col], col);
        }
    }

    std::vector<char> rowActive(n, 1);
    std::size_t remainingRows = n;

    while (remainingRows > 0) {
        while (!heap.empty()) {
            auto [weight, column] = heap.top();
            if (weight == 0 || weight != currentWeight[column]) {
                heap.pop();
            } else {
                break;
            }
        }

        if (heap.empty()) {
            throw std::runtime_error("Triangulation failed: no column with positive weight.");
        }

        auto [weight, column] = heap.top();
        heap.pop();

        // Get rows for this column
        auto rowsInCol = getRowsForCol(column);
        std::vector<std::size_t> activeRows;
        activeRows.reserve(weight);
        
        for (auto rowIndex : rowsInCol) {
            if (rowActive[rowIndex]) {
                activeRows.push_back(rowIndex);
            }
        }

        if (activeRows.empty()) {
             // This column's rows were all removed already
             currentWeight[column] = 0; // Ensure we don't pick it again
             continue;
        }

        const std::size_t pivotRow = activeRows.front();
        result.mainCols.push_back(column);
        result.mainRows.push_back(pivotRow);
        rowActive[pivotRow] = 0;
        --remainingRows;

        for (std::size_t idx = 1; idx < activeRows.size(); ++idx) {
            auto gapRow = activeRows[idx];
            result.gapRows.push_back(GapRowInfo{gapRow, pivotRow});
            rowActive[gapRow] = 0;
            --remainingRows;
        }

        for (auto removedRow : activeRows) {
            for (auto col : rows[removedRow].sparse) {
                if (currentWeight[col] == 0) {
                    continue;
                }
                --currentWeight[col];
                heap.emplace(currentWeight[col], col);
            }
        }
    }

    return result;
}

FCInverse buildFCInverse(const TriangulationInfo& tri,
                         const std::vector<RowData>& rows,
                         std::size_t sparseColumns) {
    FCInverse result;
    const auto g = tri.gapRows.size();
    if (g == 0) {
        return result;
    }

    result.dependencies.resize(g);
    const auto delta = tri.mainRows.size();
    if (delta == 0) {
        throw std::runtime_error("Gap rows exist but no main rows were selected.");
    }

    std::vector<std::size_t> mainColsReversed = tri.mainCols;
    std::reverse(mainColsReversed.begin(), mainColsReversed.end());
    std::vector<std::size_t> colMapping(sparseColumns, kInvalidIndex);
    for (std::size_t i = 0; i < mainColsReversed.size(); ++i) {
        auto column = mainColsReversed[i];
        if (column >= sparseColumns) {
            throw std::runtime_error("Main column index out of range.");
        }
        colMapping[column] = i;
    }

    auto invertRowIdx = [delta](std::size_t idx) { return delta - idx - 1; };

    for (std::size_t i = 0; i < g; ++i) {
        const auto gapInfo = tri.gapRows[i];
        const auto& gapSparse = rows[gapInfo.row].sparse;
        const auto& pivotSparse = rows[gapInfo.pivot].sparse;

        if (gapSparse == pivotSparse) {
            result.dependencies[i].push_back(gapInfo.pivot);
            continue;
        }

        std::set<std::size_t, std::greater<std::size_t>> active;//当前 Gap 行中尚未被消除的、属于 $F$ 块的列的集合。存的是 mainColsReversed 里的下标）
        for (auto column : gapSparse) {
            if (column >= sparseColumns) {
                throw std::runtime_error("Sparse column index out of range while building FCInv.");
            }
            auto mapped = colMapping[column];
            if (mapped != kInvalidIndex) {
                active.insert(mapped);
            }
        }

        while (!active.empty()) {
            auto cCol = *active.begin();
            auto hRow = tri.mainRows[invertRowIdx(cCol)];
            result.dependencies[i].push_back(hRow);

            for (auto column : rows[hRow].sparse) {
                if (column >= sparseColumns) {
                    throw std::runtime_error("Sparse column index out of range while updating FCInv.");
                }
                auto mapped = colMapping[column];
                if (mapped == kInvalidIndex) {
                    continue;
                }
                auto it = active.find(mapped);
                if (it == active.end()) {
                    active.insert(mapped);
                } else {
                    active.erase(it);
                }
            }

        }
    }

    return result;
}

std::vector<GF128> computeGapRightHandSide(const TriangulationInfo& tri,
                                           std::span<const GF128> values,
                                           const FCInverse& fcInv) {
    const auto g = tri.gapRows.size();
    if (g != fcInv.dependencies.size()) {
        throw std::runtime_error("FCInv size mismatch.");
    }
    std::vector<GF128> rhs(g, GF128::zero());
    for (std::size_t i = 0; i < g; ++i) {
        const auto gapRow = tri.gapRows[i].row;
        if (gapRow >= values.size()) {
            throw std::runtime_error("Value index out of range while computing RHS.");
        }
        rhs[i] = values[gapRow];
        for (auto helperRow : fcInv.dependencies[i]) {
            if (helperRow >= values.size()) {
                throw std::runtime_error("Helper row index out of range while computing RHS.");
            }
            rhs[i] += values[helperRow];
        }
    }
    return rhs;
}

bool solveDenseGapSystem(const TriangulationInfo& tri,
                         const std::vector<RowData>& rows,
                         const FCInverse& fcInv,
                         std::size_t denseColumns,
                         const std::vector<GF128>& rhs,
                         std::vector<std::size_t>& chosenDenseCols,
                         std::vector<GF128>& denseSolution) {
    const auto g = tri.gapRows.size();
    if (g == 0) {
        denseSolution.assign(denseSolution.size(), GF128::zero());
        chosenDenseCols.clear();
        return true;
    }

    if (denseColumns < g) {
        return false;
    }
    if (rhs.size() != g) {
        throw std::runtime_error("Right-hand side size mismatch.");
    }

    auto combined = buildCombinedDense(tri, rows, fcInv, denseColumns);
    auto rhsCopy = rhs;
    if (!solveFullRowRankSystem(combined, rhsCopy, chosenDenseCols)) {
        denseSolution.assign(denseColumns, GF128::zero());
        return false;
    }

    denseSolution.assign(denseColumns, GF128::zero());
    for (std::size_t row = 0; row < g; ++row) {
        denseSolution[chosenDenseCols[row]] = rhsCopy[row];
    }
    return true;
}

} // namespace okvs
