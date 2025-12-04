#include "okvs/encoder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace okvs {

namespace {

using Matrix = std::vector<std::vector<GF128>>;

struct TriangulationResult {
    std::vector<std::size_t> mainRows;
    std::vector<std::size_t> mainCols;
    std::vector<std::size_t> gapRows;
};

bool containsSparse(const RowData& row, std::size_t column) {
    return std::binary_search(row.sparse.begin(), row.sparse.end(), column);
}

TriangulationResult triangulateSparse(const std::vector<RowData>& rows, std::size_t sparseColumns) {
    const std::size_t n = rows.size();
    std::vector<std::vector<std::size_t>> columnToRows(sparseColumns);
    for (std::size_t r = 0; r < n; ++r) {
        for (auto column : rows[r].sparse) {
            if (column >= sparseColumns) {
                throw std::runtime_error("Sparse column index out of range during triangulation.");
            }
            columnToRows[column].push_back(r); //record the sparse
        }
    }

    std::vector<std::size_t> currentWeight(sparseColumns, 0);
    using HeapEntry = std::pair<std::size_t, std::size_t>;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> heap;

    for (std::size_t col = 0; col < sparseColumns; ++col) {
        currentWeight[col] = columnToRows[col].size();
        if (currentWeight[col] != 0) {
            heap.emplace(currentWeight[col], col); //count the weight and build the heap
        }
    }

    TriangulationResult result;
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

        std::vector<std::size_t> activeRows;
        activeRows.reserve(weight);
        for (auto rowIndex : columnToRows[column]) {
            if (rowActive[rowIndex]) {
                activeRows.push_back(rowIndex);
            }
        }

        if (activeRows.empty()) {
            continue;
        }

        const std::size_t pivotRow = activeRows.front();
        result.mainCols.push_back(column);
        result.mainRows.push_back(pivotRow);
        rowActive[pivotRow] = 0;
        --remainingRows;

        for (std::size_t idx = 1; idx < activeRows.size(); ++idx) {
            auto gapRow = activeRows[idx];
            result.gapRows.push_back(gapRow);
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

Matrix makeZeroMatrix(std::size_t rows, std::size_t cols) {
    return Matrix(rows, std::vector<GF128>(cols, GF128::zero()));
}

Matrix subtractMatrix(const Matrix& lhs, const Matrix& rhs) {
    if (lhs.size() != rhs.size()) {
        throw std::runtime_error("Matrix subtraction dimension mismatch.");
    }
    Matrix result = lhs;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].size() != rhs[i].size()) {
            throw std::runtime_error("Matrix subtraction inner dimension mismatch.");
        }
        for (std::size_t j = 0; j < lhs[i].size(); ++j) {
            result[i][j] -= rhs[i][j];
        }
    }
    return result;
}

Matrix multiplyMatrix(const Matrix& lhs, const Matrix& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return makeZeroMatrix(lhs.size(), rhs.empty() ? 0 : rhs[0].size());
    }
    const std::size_t rows = lhs.size();
    const std::size_t mid = lhs[0].size();
    if (rhs.size() != mid) {
        throw std::runtime_error("Matrix multiplication dimension mismatch.");
    }
    const std::size_t cols = rhs[0].size();
    Matrix result = makeZeroMatrix(rows, cols);
    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t k = 0; k < mid; ++k) {
            const GF128 factor = lhs[i][k];
            if (factor.isZero()) {
                continue;
            }
            for (std::size_t j = 0; j < cols; ++j) {
                result[i][j] += factor * rhs[k][j];
            }
        }
    }
    return result;
}

std::vector<GF128> multiplyMatrixVector(const Matrix& mat, const std::vector<GF128>& vec) {
    std::vector<GF128> result(mat.size(), GF128::zero());
    if (mat.empty() || vec.empty()) {
        return result;
    }
    for (std::size_t i = 0; i < mat.size(); ++i) {
        if (mat[i].size() != vec.size()) {
            throw std::runtime_error("Matrix-vector multiplication dimension mismatch.");
        }
        for (std::size_t j = 0; j < vec.size(); ++j) {
            if (mat[i][j].isZero() || vec[j].isZero()) {
                continue;
            }
            result[i] += mat[i][j] * vec[j];
        }
    }
    return result;
}

Matrix invertLowerTriangular(const Matrix& F) {
    const std::size_t n = F.size();
    if (n == 0) {
        return {};
    }
    Matrix inverse = makeZeroMatrix(n, n);
    for (std::size_t col = 0; col < n; ++col) {
        std::vector<GF128> rhs(n, GF128::zero());
        rhs[col] = GF128::one();
        std::vector<GF128> solution(n, GF128::zero());
        for (std::size_t row = 0; row < n; ++row) {
            GF128 acc = rhs[row];
            for (std::size_t k = 0; k < row; ++k) {
                acc -= F[row][k] * solution[k];
            }
            const GF128 diag = F[row][row];
            if (diag.isZero()) {
                throw std::runtime_error("Singular lower-triangular matrix encountered in F.");
            }
            if (!(diag == GF128::one())) {
                acc *= diag.inverse();
            }
            solution[row] = acc;
        }
        for (std::size_t row = 0; row < n; ++row) {
            inverse[row][col] = solution[row];
        }
    }
    return inverse;
}

struct RowOperation {
    enum class Type { Swap, Scale, Add };
    Type type;
    std::size_t target;
    std::size_t other;
    GF128 factor;
};

void applyRowOperation(const RowOperation& op, Matrix& matrix) {
    if (matrix.empty()) {
        return;
    }
    switch (op.type) {
        case RowOperation::Type::Swap:
            std::swap(matrix[op.target], matrix[op.other]);
            break;
        case RowOperation::Type::Scale:
            for (auto& value : matrix[op.target]) {
                value *= op.factor;
            }
            break;
        case RowOperation::Type::Add:
            for (std::size_t i = 0; i < matrix[op.target].size(); ++i) {
                matrix[op.target][i] -= op.factor * matrix[op.other][i];
            }
            break;
    }
}

void applyRowOperation(const RowOperation& op, std::vector<GF128>& vec) {
    if (vec.empty()) {
        return;
    }
    switch (op.type) {
        case RowOperation::Type::Swap:
            std::swap(vec[op.target], vec[op.other]);
            break;
        case RowOperation::Type::Scale:
            vec[op.target] *= op.factor;
            break;
        case RowOperation::Type::Add:
            vec[op.target] -= op.factor * vec[op.other];
            break;
    }
}

struct DenseEliminationResult {
    Matrix A;
    Matrix B;
    std::vector<RowOperation> operations;
    std::vector<std::size_t> denseOrder;
};

DenseEliminationResult reduceDenseBlock(Matrix A, Matrix B) {
    const std::size_t g = B.size();
    const std::size_t denseCols = g == 0 ? 0 : B[0].size();
    std::vector<std::size_t> order;
    order.reserve(g);

    std::vector<RowOperation> ops;
    if (g == 0) {
        return {A, B, ops, order};
    }

    std::size_t pivotRow = 0;
    for (std::size_t col = 0; col < denseCols && pivotRow < g; ++col) {
        std::size_t candidate = pivotRow;
        while (candidate < g && B[candidate][col].isZero()) {
            ++candidate;
        }

        if (candidate == g) {
            continue;
        }

        if (candidate != pivotRow) {
            std::swap(B[candidate], B[pivotRow]);
            std::swap(A[candidate], A[pivotRow]);
            ops.push_back({RowOperation::Type::Swap, pivotRow, candidate, GF128::zero()});
        }

        GF128 pivotValue = B[pivotRow][col];
        if (pivotValue.isZero()) {
            throw std::runtime_error("Unexpected zero pivot during dense elimination.");
        }
        GF128 inv = pivotValue.inverse();
        if (!(inv == GF128::one())) {
            for (auto& value : B[pivotRow]) {
                value *= inv;
            }
            for (auto& value : A[pivotRow]) {
                value *= inv;
            }
            ops.push_back({RowOperation::Type::Scale, pivotRow, 0, inv});
        }

        for (std::size_t row = 0; row < g; ++row) {
            if (row == pivotRow) {
                continue;
            }
            GF128 factor = B[row][col];
            if (factor.isZero()) {
                continue;
            }
            for (std::size_t c = 0; c < denseCols; ++c) {
                B[row][c] -= factor * B[pivotRow][c];
            }
            for (std::size_t c = 0; c < A[row].size(); ++c) {
                A[row][c] -= factor * A[pivotRow][c];
            }
            ops.push_back({RowOperation::Type::Add, row, pivotRow, factor});
        }

        order.push_back(col);
        ++pivotRow;
    }

    if (pivotRow != g) {
        throw std::runtime_error("B' does not have full row rank.");
    }

    return {A, B, ops, order};
}

struct TransformedValues {
    std::vector<GF128> top;
    std::vector<GF128> bottom;
};

TransformedValues transformValues(const std::vector<GF128>& values,
                                  const std::vector<std::size_t>& rowPermutation,
                                  const Matrix& X,
                                  const std::vector<RowOperation>& rowOps,
                                  std::size_t gap,
                                  std::size_t delta) {
    const std::size_t n = gap + delta;
    if (rowPermutation.size() != n) {
        throw std::runtime_error("Row permutation length mismatch.");
    }
    if (values.size() < n) {
        throw std::runtime_error("Value vector shorter than permutation length.");
    }

    std::vector<GF128> permuted(n);
    for (std::size_t i = 0; i < n; ++i) {
        permuted[i] = values[rowPermutation[i]];
    }

    TransformedValues result;
    result.top.assign(permuted.begin(), permuted.begin() + gap);
    result.bottom.assign(permuted.begin() + gap, permuted.end());

    if (gap > 0 && !X.empty()) {
        auto correction = multiplyMatrixVector(X, result.bottom);
        if (correction.size() != result.top.size()) {
            throw std::runtime_error("Correction vector size mismatch.");
        }
        for (std::size_t i = 0; i < result.top.size(); ++i) {
            result.top[i] -= correction[i];
        }
    }

    for (const auto& op : rowOps) {
        applyRowOperation(op, result.top);
    }

    return result;
}

std::vector<std::size_t> buildDenseCompleteOrder(const std::vector<std::size_t>& denseOrder,
                                                 std::size_t dense) {
    std::vector<std::size_t> complete;
    complete.reserve(dense);
    std::vector<char> used(dense, 0);
    for (auto idx : denseOrder) {
        if (idx >= dense) {
            throw std::runtime_error("Dense permutation index out of range.");
        }
        if (!used[idx]) {
            complete.push_back(idx);
            used[idx] = 1;
        }
    }
    for (std::size_t j = 0; j < dense; ++j) {
        if (!used[j]) {
            complete.push_back(j);
        }
    }
    return complete;
}

std::vector<GF128> solveDensePart(const std::vector<GF128>& transformedTop,
                                  const std::vector<std::size_t>& denseOrder,
                                  std::size_t denseCols) {
    if (denseOrder.size() != transformedTop.size()) {
        throw std::runtime_error("Dense pivot count mismatch during solve.");
    }
    std::vector<GF128> pdense(denseCols, GF128::zero());
    for (std::size_t i = 0; i < denseOrder.size(); ++i) {
        auto idx = denseOrder[i];
        if (idx >= denseCols) {
            throw std::runtime_error("Dense permutation index out of range.");
        }
        pdense[idx] = transformedTop[i];
    }
    return pdense;
}

std::vector<GF128> solveSparseTrunk(const Matrix& F,
                                    const Matrix& E,
                                    const std::vector<GF128>& VBottom,
                                    const std::vector<GF128>& PDense) {
    const std::size_t delta = F.size();
    std::vector<GF128> rhs = VBottom;
    if (rhs.size() != delta) {
        throw std::runtime_error("VBottom length mismatch with F.");
    }

    if (!E.empty()) {
        for (std::size_t i = 0; i < delta; ++i) {
            for (std::size_t j = 0; j < E[i].size(); ++j) {
                if (!E[i][j].isZero() && j < PDense.size() && !PDense[j].isZero()) {
                    rhs[i] -= E[i][j] * PDense[j];
                }
            }
        }
    }

    std::vector<GF128> PF(delta, GF128::zero());
    for (std::size_t i = 0; i < delta; ++i) {
        GF128 acc = rhs[i];
        for (std::size_t j = 0; j < i; ++j) {
            if (!F[i][j].isZero()) {
                acc -= F[i][j] * PF[j];
            }
        }
        const GF128 diag = F[i][i];
        if (diag.isZero()) {
            throw std::runtime_error("Diagonal entry of F is zero.");
        }
        if (!(diag == GF128::one())) {
            acc *= diag.inverse();
        }
        PF[i] = acc;
    }
    return PF;
}

} // namespace

OkvsEncoder::OkvsEncoder(OkvsConfig config, std::uint64_t seed)
    : mConfig(config),
      mSeed(seed) {}

std::size_t OkvsEncoder::sparseSize(std::size_t numItems) const {
    if (mConfig.explicitSparseSize != 0) {
        return mConfig.explicitSparseSize;
    }
    return static_cast<std::size_t>(std::ceil(mConfig.sparseExpansion * static_cast<double>(numItems)));
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

OkvsEncoder::EncodedTable OkvsEncoder::encode(const std::vector<std::string>& keys,
                                              const std::vector<GF128>& values) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/value vector sizes.");
    }

    const std::size_t n = keys.size();
    RowHasher hasher = makeHasher(n);
    const std::size_t sparse = hasher.sparseSize();
    const std::size_t dense = hasher.denseSize();
    const std::size_t totalColumns = sparse + dense;

    std::vector<RowData> rows;
    rows.reserve(n);
    for (const auto& key : keys) {
        rows.emplace_back(hasher.generate(key));
    }

    TriangulationResult tri = triangulateSparse(rows, sparse);
    const std::size_t delta = tri.mainRows.size();
    const std::size_t gap = tri.gapRows.size();
    if (delta + gap != n) {
        throw std::runtime_error("Triangulation result inconsistent with input size.");
    }

    std::vector<char> isMainColumn(sparse, 0);
    for (auto col : tri.mainCols) {
        isMainColumn[col] = 1; //record the main column
    }

    std::vector<std::size_t> nonMainCols;
    nonMainCols.reserve(sparse - delta);
    for (std::size_t col = 0; col < sparse; ++col) {
        if (!isMainColumn[col]) {
            nonMainCols.push_back(col); //record the non-main column
        }
    }

    std::vector<std::size_t> mainColsReversed = tri.mainCols;
    std::reverse(mainColsReversed.begin(), mainColsReversed.end());
    std::vector<std::size_t> mainRowsReversed = tri.mainRows;
    std::reverse(mainRowsReversed.begin(), mainRowsReversed.end());

    std::vector<std::size_t> rowPermutation;
    rowPermutation.reserve(n);
    rowPermutation.insert(rowPermutation.end(), tri.gapRows.begin(), tri.gapRows.end());
    rowPermutation.insert(rowPermutation.end(), mainRowsReversed.begin(), mainRowsReversed.end());

    std::vector<std::size_t> nonMainPosition(sparse, static_cast<std::size_t>(-1));
    for (std::size_t idx = 0; idx < nonMainCols.size(); ++idx) {
        nonMainPosition[nonMainCols[idx]] = idx; //nonMainPosition[H.c]=T.c
    }
    std::vector<std::size_t> mainPosition(sparse, static_cast<std::size_t>(-1));
    for (std::size_t idx = 0; idx < mainColsReversed.size(); ++idx) {
        mainPosition[mainColsReversed[idx]] = idx; //mainPosition[H.c]=T.c
    }

    Matrix A = makeZeroMatrix(gap, nonMainCols.size());
    Matrix B = makeZeroMatrix(gap, dense);
    Matrix C = makeZeroMatrix(gap, delta);
    for (std::size_t i = 0; i < gap; ++i) {
        const auto rowIndex = tri.gapRows[i];
        for (auto column : rows[rowIndex].sparse) {
            auto pos = nonMainPosition[column];
            if (pos != static_cast<std::size_t>(-1)) {
                A[i][pos] = GF128::one();
            } else {
                auto mainPos = mainPosition[column];
                if (mainPos != static_cast<std::size_t>(-1)) {
                    C[i][mainPos] = GF128::one();
                }
            }
        }
        for (std::size_t j = 0; j < dense; ++j) {
            if (j < rows[rowIndex].dense.size()) {
                B[i][j] = rows[rowIndex].dense[j];
            }
        }
    }

    Matrix D = makeZeroMatrix(delta, nonMainCols.size());
    Matrix E = makeZeroMatrix(delta, dense);
    Matrix F = makeZeroMatrix(delta, delta);
    for (std::size_t i = 0; i < delta; ++i) {
        const auto rowIndex = mainRowsReversed[i];
        for (auto column : rows[rowIndex].sparse) {
            auto pos = nonMainPosition[column];
            if (pos != static_cast<std::size_t>(-1)) {
                D[i][pos] = GF128::one();
            } else {
                auto mainPos = mainPosition[column];
                if (mainPos != static_cast<std::size_t>(-1)) {
                    F[i][mainPos] = GF128::one();
                }
            }
        }
        for (std::size_t j = 0; j < dense; ++j) {
            if (j < rows[rowIndex].dense.size()) {
                E[i][j] = rows[rowIndex].dense[j];
            }
        }
    }

//-------------------------------------------C:=0-------------------------------------------
    Matrix FInverse = invertLowerTriangular(F); //calculate F^-1
    Matrix X = multiplyMatrix(C, FInverse); //calculate X=C*F^-1
    Matrix APrime = subtractMatrix(A, multiplyMatrix(X, D)); //calculate A'=A-(C * F^-1) * D
    Matrix BPrime = subtractMatrix(B, multiplyMatrix(X, E)); //calculate B'=B-(C * F^-1) * E
//------------------------------------------------------------------------------------------

    DenseEliminationResult denseResult = reduceDenseBlock(APrime, BPrime);
    const auto& rowOps = denseResult.operations;
    const auto& denseOrder = denseResult.denseOrder;
    auto denseCompleteOrder = buildDenseCompleteOrder(denseOrder, dense);

    auto transformed = transformValues(values, rowPermutation, X, rowOps, gap, delta);//calculate V'
    auto PDense = solveDensePart(transformed.top, denseOrder, dense);//calculate I*P(dense)=V(top)
    auto PF = solveSparseTrunk(F, E, transformed.bottom, PDense); //calculate F*P(spares)=V(bottom)-E*P(dense)

    std::vector<std::size_t> columnOrder;
    columnOrder.reserve(totalColumns);
    for (auto col : nonMainCols) {
        columnOrder.push_back(col);
    }
    for (auto denseIdx : denseCompleteOrder) {
        columnOrder.push_back(sparse + denseIdx);
    }
    for (auto col : mainColsReversed) {
        columnOrder.push_back(col);
    }

    std::vector<GF128> PStar(totalColumns, GF128::zero());
    std::size_t cursor = 0;
    cursor += nonMainCols.size(); // remains zero
    for (auto idx : denseCompleteOrder) {
        PStar[cursor++] = PDense[idx];
    }
    for (std::size_t i = 0; i < PF.size(); ++i, ++cursor) {
        PStar[cursor] = PF[i];
    }

    std::vector<GF128> encoded(totalColumns, GF128::zero());
    for (std::size_t i = 0; i < totalColumns; ++i) {
        const auto originalColumn = columnOrder[i];
        if (originalColumn >= totalColumns) {
            throw std::runtime_error("Column permutation out of range.");
        }
        encoded[originalColumn] = PStar[i];
    }

    EncodedTable table;
    table.data = std::move(encoded);
    table.sparseColumns = sparse;
    table.denseColumns = dense;
    table.gap = gap;
    table.delta = delta;

    return table;
}

GF128 OkvsEncoder::decode(const std::string& key, const EncodedTable& table) const {
    RowHasher hasher(table.sparseColumns, table.denseColumns, mConfig.weight, mSeed);
    RowData row = hasher.generate(key);
    GF128 acc = GF128::zero();
    for (auto idx : row.sparse) {
        if (idx >= table.sparseColumns) {
            throw std::runtime_error("Sparse index out of range during decode.");
        }
        acc += table.data[idx];
    }
    for (std::size_t j = 0; j < row.dense.size(); ++j) {
        if (j >= table.denseColumns) {
            throw std::runtime_error("Dense index out of range during decode.");
        }
        acc += row.dense[j] * table.data[table.sparseColumns + j];
    }
    return acc;
}

} // namespace okvs

