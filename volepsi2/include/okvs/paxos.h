#pragma once

#include "okvs/gf128.h"
#include "okvs/row_hasher.h"

#include <cstddef>
#include <span>
#include <vector>

namespace okvs {

struct GapRowInfo {
    std::size_t row = 0;
    std::size_t pivot = 0;
};

struct TriangulationInfo {
    std::vector<std::size_t> mainRows;
    std::vector<std::size_t> mainCols;
    std::vector<GapRowInfo> gapRows;
};

struct FCInverse {
    std::vector<std::vector<std::size_t>> dependencies;
};

TriangulationInfo triangulateRows(const std::vector<RowData>& rows, std::size_t sparseColumns);

FCInverse buildFCInverse(const TriangulationInfo& tri,
                         const std::vector<RowData>& rows,
                         std::size_t sparseColumns);

std::vector<GF128> computeGapRightHandSide(const TriangulationInfo& tri,
                                           std::span<const GF128> values,
                                           const FCInverse& fcInv);

bool solveDenseGapSystem(const TriangulationInfo& tri,
                         const std::vector<RowData>& rows,
                         const FCInverse& fcInv,
                         std::size_t denseColumns,
                         const std::vector<GF128>& rhs,
                         std::vector<std::size_t>& chosenDenseCols,
                         std::vector<GF128>& denseSolution);

} // namespace okvs


