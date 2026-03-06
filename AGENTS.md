# Repository Guidelines

## Project Background
[`paper.md`](/home/zen/Project/okvs-psi-impl/paper.md) is the core paper for this repository. The project reproduces that paper step by step and will later add an interface that shows the reproduction details and results. Active implementation lives in [`volepsi2/`](/home/zen/Project/okvs-psi-impl/volepsi2), where the OKVS portion has already been reproduced. [`volepsi/`](/home/zen/Project/okvs-psi-impl/volepsi) is the paper’s reference source tree and currently includes temporary local changes for OKVS benchmarking.

## Project Structure & Module Organization
Use [`volepsi2/`](/home/zen/Project/okvs-psi-impl/volepsi2) for new reproduction and interface work. Public OKVS headers are in [`volepsi2/include/okvs/`](/home/zen/Project/okvs-psi-impl/volepsi2/include/okvs), implementations are in [`volepsi2/src/`](/home/zen/Project/okvs-psi-impl/volepsi2/src), and focused tests are in [`volepsi2/tests/`](/home/zen/Project/okvs-psi-impl/volepsi2/tests). Use [`volepsi/volePSI/`](/home/zen/Project/okvs-psi-impl/volepsi/volePSI), [`volepsi/frontend/`](/home/zen/Project/okvs-psi-impl/volepsi/frontend), and [`volepsi/tests/`](/home/zen/Project/okvs-psi-impl/volepsi/tests) as reference and benchmark support. Ignore generated output in `volepsi/out/` and `volepsi2/build/`.

## Build, Test, and Development Commands
Use `python3 volepsi/build.py --par=8` to build the integrated tree in `volepsi/out/build/linux`. Use `python3 volepsi/build.py --debug` for debug builds and pass extra CMake flags such as `-DVOLE_PSI_ENABLE_ASAN=ON` when needed. For isolated OKVS work, run `cmake -S volepsi2 -B volepsi2/build` and `cmake --build volepsi2/build`. Run CTest with `ctest --test-dir volepsi/out/build/linux --output-on-failure`, and run the broader protocol suite with `./volepsi/out/build/linux/frontend -u` or `-u -list`.

## Coding Style & Naming Conventions
Target C++20. Use 4-space indentation, keep includes ordered local-to-external, and avoid broad formatting rewrites. Match nearby code: `volepsi` mostly uses Allman braces and `PascalCase` types, while `volepsi2` uses modern header style, `camelCase` methods, and `mMember` fields. Follow existing test names: `*_Tests.cpp` in `volepsi/tests/` and `*_test.cpp` in `volepsi2/tests/`. No formatter or linter is checked in, so consistency with the touched file is the rule.

## Testing Guidelines
Add or update a focused test for every protocol, OKVS, or GF(2^128) change. Prefer deterministic inputs and cover edge sizes, reduced-round or multithreaded paths, and any temporary `volepsi` benchmark hooks you touch. For performance-sensitive work, record the exact benchmark command, for example `./volepsi/out/build/linux/frontend -perf -psi -nn 20 -t 1`, and summarize before/after results in the review.

## Commit & Pull Request Guidelines
Git history favors short, imperative commit subjects such as `Add clustered encoder module` or `fix gitignore for build dir`. Keep commits narrowly scoped, and separate `volepsi2` reproduction work from temporary `volepsi` benchmark edits or submodule pointer updates. Pull requests should name the affected paper section, list validation commands, and include benchmark data for hot-path changes. Add screenshots only when interface work lands.
