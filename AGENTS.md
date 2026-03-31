# Repository Guidelines

## Project Structure & Module Organization

`volepsi2/` is the main implementation and the final Web UI deliverable. Public headers live in `volepsi2/include/okvs/`, core implementations are in `volepsi2/src/`, demo assets are in `volepsi2/demo/`, and focused regression tests are in `volepsi2/tests/`. Treat upstream `volepsi` as historical reference context only, not as a required sibling tree. Keep `paper.md` as the algorithm reference and `EVALUATION.md` as the benchmark record. Ignore generated output in `volepsi2/build/`.

## Build, Test, and Development Commands

- `cmake -S volepsi2 -B volepsi2/build`: configure the standalone reproduction build.
- `cmake --build volepsi2/build --target core_test psi_test volepsi2_bench volepsi2_demo`: build the main test, benchmark, and demo targets.
- `ctest --test-dir volepsi2/build --output-on-failure`: run the regression suite.
- `./volepsi2/build/volepsi2_demo --port 8090`: launch the final Web UI locally.
- `./volepsi2/build/volepsi2_bench psi -nn 12 -t 3 -nt 4`: run a representative local PSI benchmark.

## Coding Style & Naming Conventions

Target C++20 and use 4-space indentation. Keep includes ordered from local to external. In `volepsi2`, follow nearby style: `PascalCase` for types, `camelCase` for methods, and `mMember` for fields. Preserve the existing brace style in touched files and avoid broad formatting-only rewrites. No formatter or linter is checked in, so consistency with surrounding code is the rule.

## Testing Guidelines

Keep tests deterministic and focused. `core_test` covers GF(2^128), OKVS, and clustered OKVS; `psi_test` covers the end-to-end PSI path used by the Web UI. Add or update tests whenever protocol logic, OKVS behavior, multithreading, or telemetry changes. For performance-sensitive work, record exact benchmark commands and update `EVALUATION.md` if measured results change.

## Commit & Pull Request Guidelines

Use short, imperative commit subjects such as `Add clustered PSI telemetry` or `Fix demo asset routing`. Pull requests should name the affected paper section, list validation commands, include benchmark deltas for hot paths, and add screenshots when the Web UI changes.
