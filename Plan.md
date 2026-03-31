# Undergraduate Graduation Project Plan

## Core Deliverables
- [x] Implement the paper's semi-honest PSI pipeline in `volepsi2`.
Goal: move beyond the reproduced OKVS and complete the Section 4 flow from `Encode` to final intersection output.

- [x] Keep upstream `volepsi` as a reference and benchmark baseline.
Goal: use upstream `volepsi` results to validate ideas and compare performance, but keep the final deliverable centered on `volepsi2`.

- [x] Build a correctness test suite for the reproduced pipeline.
Goal: cover OKVS encode/decode, PSI correctness, empty sets, full intersection, partial intersection, and uneven set sizes.

- [x] Run a structured performance evaluation.
Goal: measure OKVS encode/decode time, PSI runtime, and single-thread versus multi-thread behavior, then compare against `volepsi` and the trends reported in `paper.md`.

- [x] Build a demo interface in `volepsi2`.
Goal: provide a simple interface that can load or generate sets, show important intermediate steps, display the final intersection, and report timing results.

## Documentation and Submission Work
- [x] Document all deviations from the paper.
Goal: clearly state what has been reproduced, what has been simplified, what is still missing, and what temporary upstream comparisons were used for testing.

- [x] Improve engineering quality in `volepsi2`.
Goal: clean up module boundaries, add clear usage instructions, make benchmark commands repeatable, and ensure tests are easy to run.

- [ ] Prepare thesis/report materials.
Goal: summarize background, design choices, implementation details, experimental results, and lessons learned in a form suitable for the graduation report and defense.

## Stretch Goals
- [ ] Implement the low-communication subfield-VOLE variant.
- [ ] Add DOKVS / circuit-PSI support.
- [ ] Explore optimal triangulation and other paper-level optimizations.

## Recommended Order
1. Finish PSI in `volepsi2`.
2. Add correctness tests.
3. Run benchmarks and compare with upstream `volepsi`.
4. Build the demo interface.
5. Finalize documentation and thesis materials.
6. Attempt stretch goals only if time remains.
