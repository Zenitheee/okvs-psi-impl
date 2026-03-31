# Implementation Gaps and Rigor Backlog

This file records the material implementation choices in `volepsi2` that are heuristic, demo-oriented, or otherwise not formally matched to the paper. The goal is to make the remaining work explicit for future implementation.

## Scope

This audit covers the graduation-project implementation in `volepsi2/`, especially:

- OKVS encode/decode
- clustered OKVS
- semi-honest PSI fast instantiation

The older `okvs-psi-origin/volepsi` tree is historical reference only.

## Not Counted As Project-Specific Gaps

These are empirical concrete parameters already used or motivated by the paper, so they should not be treated as ad hoc project mistakes by themselves:

- `weight = 3`
- `sparseExpansion = 1.23`
- `binSizeHint = 2^14`

## Backlog

### [Done] 1. Replace ad hoc hash and random-oracle instantiation

- Status:
  - Implemented in the current repository revision.
  - `volepsi2/src/hash_utils.h` now uses vendored `cryptoTools::RandomOracle` (Blake2) with explicit domain separation.
  - The protocol-facing call sites were switched in `row_hasher`, `binned_encoder`, and `psi`.

- Evidence:
  - `volepsi2/src/hash_utils.h:13-60`
  - `volepsi2/src/binned_encoder.cpp:67-74`
  - `volepsi2/src/psi.cpp:180-189`
- Current behavior:
  - Uses custom `splitMix64`-based mixing for byte hashing.
  - Uses an FNV-like bin hash plus `splitMix64`.
  - Uses custom `hashToBaseField` and `hashOutput` in PSI.
- Gap vs paper:
  - The paper assumes random functions / random oracles for `row`, `H^B`, and `H^o`.
- Risk:
  - No cryptographic justification for collision resistance or pseudorandomness.
  - Suitable for demonstration and benchmarking, not for proof-grade claims.
- Workload:
  - Medium.
- Suggested implementation:
  1. Replace with AES-based PRF, BLAKE3, or SHAKE with domain separation.
  2. Derive sparse-row randomness, base-field hashing, output hashing, and bin mapping from separate labeled contexts.
  3. Document the security assumption used by the hash layer.

### [Done] 2. Replace deterministic session randomness with fresh cryptographic randomness

- Status:
  - Implemented in the current repository revision.
  - `volepsi2/src/psi.cpp` now seeds session randomness from `osuCrypto::sysRandomSeed()` by default and uses `osuCrypto::PRNG` for session randomness expansion.
  - Deterministic seeding remains available only through explicit `PsiConfig::deterministicSeedEnabled = true`.
  - `PsiResult` and `PsiTelemetry` now expose `usedDeterministicSeed` so demo and benchmark outputs can distinguish reproducible runs from fresh-random runs.

- Evidence:
  - `volepsi2/include/okvs/psi.h:14-19`
  - `volepsi2/src/psi.cpp:146-158`
  - `volepsi2/src/psi.cpp:412-418`
  - `volepsi2/src/psi.cpp:592-598`
- Current behavior:
  - Default seed is fixed.
  - `SessionRng` is `splitMix64`-based.
  - Session randomness depends only on the configured seed and input sizes.
- Gap vs paper:
  - The paper samples fresh random seeds per protocol instance.
- Risk:
  - Reused randomness is not acceptable for a real cryptographic deployment.
  - Fine only for reproducible demo and benchmark runs.
- Workload:
  - Low to medium.
- Suggested implementation:
  1. Seed a cryptographic PRG from OS randomness by default.
  2. Keep deterministic seeding only as an opt-in benchmark mode.
  3. Surface the distinction clearly in the demo and telemetry.

### [Done] 3. Remove modulo bias from sparse index extraction

- Status:
  - Implemented in the current repository revision.
  - `volepsi2/src/row_hasher.cpp` now uses rejection sampling for unbiased range reduction from 32-bit AES output words.
  - Sparse-row support is still sampled without replacement, but now from unbiased candidate indices rather than `% mMPrime`.
  - `volepsi2/tests/core_test.cpp` adds regression coverage for sparse-row size, ordering, uniqueness, and range invariants.

- Evidence:
  - `volepsi2/src/row_hasher.cpp`
  - `volepsi2/tests/core_test.cpp`
- Current behavior:
  - Extracts 32-bit chunks from the AES-derived random stream.
  - Rejects out-of-range samples so each accepted candidate is uniform in `[0, m')`.
  - Accepts candidates only if they are not already present in the sparse row.
- Gap vs paper:
  - The sparse support should be sampled as a uniformly random weight-`w` subset.
- Risk:
  - Remaining behavior depends on the deterministic row PRF construction, but the modulo-reduction bias itself is removed.
- Workload:
  - Low.

### [High] 4. Replace hashed-bin clustered OKVS with paper-faithful clustered row construction

- Evidence:
  - `volepsi2/src/binned_encoder.cpp:23-42`
  - `volepsi2/src/binned_encoder.cpp:56-100`
- Current behavior:
  - Hashes each item into one independent bin.
  - Runs a separate OKVS instance per bin.
  - Allocates a full dense tail for every bin.
- Gap vs paper:
  - The paper's clustering is defined by restricting sparse row support to a cluster and adapting `hat row`.
  - The current code implements a simpler hashed-bin design instead.
- Risk:
  - No direct proof match to the paper's clustered OKVS analysis.
  - Table size and failure behavior differ from the paper.
- Notes:
  - A local benchmark at `n = 4096`, `binSizeHint = 2048` produced `table_size = 5886` for clustered OKVS versus `5079` for unclustered OKVS, showing that the current clustered path is conservative rather than tight.
- Workload:
  - High.
- Suggested implementation:
  1. Redesign row generation so sparse support lies inside a chosen cluster of size `m*`.
  2. Implement either the paper's combined or separate dense-column strategy.
  3. Derive `beta`, `n*`, and `hat g` from an explicit bound instead of runtime overflow checks.

### [Medium] 5. Replace heuristic bin-cap formula with a proof-backed overflow bound

- Evidence:
  - `volepsi2/src/binned_encoder.cpp:56-64`
- Current behavior:
  - Uses `avg + 6 * sqrt(avg) + max(40, ssp)` as the per-bin capacity.
- Gap vs paper:
  - The paper requires an upper bound on the maximum per-cluster load in order to set `hat g`.
  - This formula is an engineering heuristic.
- Risk:
  - May still overflow in theory.
  - May over-allocate significantly in practice.
- Workload:
  - Medium.
- Suggested implementation:
  1. Use a balls-into-bins tail bound with a target failure probability.
  2. Parameterize the bound by `lambda`, `numItems`, and `numBins`.
  3. Keep empirical tuning separate from the proof-backed capacity bound.

### [Medium] 6. Tie dense-column count to a proved gap bound

- Evidence:
  - `volepsi2/include/okvs/encoder.h:20-25`
  - `volepsi2/src/encoder.cpp:132-137`
- Current behavior:
  - Dense columns default to `securityParameter`.
- Gap vs paper:
  - The dense part is justified through an upper bound `hat g` and a full-rank argument for `B'`.
  - The current choice is conservative but not theorem-faithful.
- Risk:
  - Over-allocation in some modes.
  - No explicit proof link from dense width to gap behavior.
- Workload:
  - Medium.
- Suggested implementation:
  1. Decide whether the implementation follows binary `hat row` or field/Vandermonde `hat row`.
  2. Compute dense width from `hat g` and the chosen proof strategy.
  3. Document the derivation.

### [Medium] 7. Make dense gap solving exact instead of heuristic

- Evidence:
  - `volepsi2/src/paxos.cpp:344-390`
- Current behavior:
  - Tries contiguous windows of dense columns.
  - Then tries 16 random dense-column subsets.
- Gap vs paper:
  - The solver can fail even when a valid full-rank dense submatrix exists.
- Risk:
  - False encode failures unrelated to true rank deficiency.
- Workload:
  - Medium.
- Suggested implementation:
  1. Perform full pivoted elimination on the entire `g x denseColumns` matrix.
  2. Or implement the exact `B'` / `B*` construction from the paper.
  3. Keep the current heuristic only as an optional fast path after an exact fallback exists.

### [Medium-High] 8. Implement the stronger small-gap triangulation refinement

- Evidence:
  - `volepsi2/src/paxos.cpp:124-241`
  - Paper discussion: `paper.md:149-159`, `paper.md:203`, `paper.md:410-414`
- Current behavior:
  - Uses the greedy minimum-weight-column triangulation only.
- Gap vs paper:
  - For `g = 0`, the greedy step is optimal.
  - For general small `g`, it is only near-optimal / heuristically justified.
  - The modified 2-core exhaustive search is not implemented.
- Risk:
  - Gap may be larger than necessary.
- Workload:
  - Medium to high.
- Suggested implementation:
  1. Extract the 2-core.
  2. If `hat g` is known and small, exhaustively search removable rows in the 2-core.
  3. Fall back to the greedy path when the core is empty or the search budget is exceeded.

### [Low] 9. Separate demo and benchmark fallbacks from protocol claims

- Evidence:
  - `volepsi2/src/psi.cpp:275-301`
  - `volepsi2/src/psi.cpp:478-490`
  - `volepsi2/src/psi.cpp:512-559`
- Current behavior:
  - Supports simulated VOLE fallback.
  - In one path, models transfer sizes instead of actually sending data.
- Gap vs paper:
  - These are acceptable engineering fallbacks, but they are not paper-grade protocol evidence.
- Risk:
  - Mostly a documentation and presentation risk.
- Workload:
  - Low.
- Suggested implementation:
  1. Keep fallbacks, but label them explicitly as demo or benchmark modes.
  2. Do not mix modeled traffic with real measured traffic in evaluation claims.

## Recommended Implementation Order

1. Make dense gap solving exact.
2. Tie dense width to a proved gap bound.
3. Replace heuristic bin overflow sizing with a proof-backed bound.
4. Rework clustered OKVS to match the paper's clustered row construction.
5. Add stronger small-gap triangulation refinement.

## Current Project Positioning

This backlog does not mean the project is weak. It means the current repository should be presented as:

- a functional reproduction,
- a demonstrable OKVS-PSI system,
- and a sound undergraduate project,

not as a fully proof-grade or deployment-grade cryptographic implementation.

## Current Validation Status

The existing regression suite passes locally:

- `ctest --test-dir volepsi2/build --output-on-failure`
- `core_test`
- `psi_test`

This validates functionality and end-to-end behavior, not formal security or tail bounds.
