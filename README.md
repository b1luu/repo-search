# repo-search

A low-latency, dependency-aware code search engine written in C++20.

Indexes Python source files, builds an import dependency graph, and scores
search results using both lexical similarity and graph structure.

---

## Building

**Release (default)**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/repo_search <directory> [query]
```

**Debug (with ASan/UBSan)**
```bash
./scripts/build_debug.sh
# Binary: build/debug/repo_search
# Tests:  build/debug/tests/rs_tests
```

---

## Formatting

The project enforces clang-format style defined in `.clang-format`.

```bash
# Apply formatting to all source files
./scripts/format.sh

# Check without modifying (exits non-zero if any file is unformatted)
./scripts/check_format.sh
```

If `clang-format` is not in your `PATH`, set:
```bash
export CLANG_FORMAT=/path/to/clang-format
```

---

## Testing

Tests are registered with CTest.  Run them via:

```bash
ctest --test-dir build/debug --output-on-failure
```

Or run the test binary directly for detailed output:
```bash
./build/debug/tests/rs_tests
```

---

## Benchmarking

`scripts/bench_search.sh` runs `repo_search` multiple times against a corpus
and reports min / median / p95 for the search hot path, plus medians for the
parse / index / graph phases. Bash 3 compatible; no Python required.

```bash
# Default: 7 runs against the debug binary
./scripts/bench_search.sh corpus/ucsd-classrooms "classroom schedule"

# Override run count and/or binary path
RUNS=25 ./scripts/bench_search.sh corpus/fastapi "dependency injection"
BIN=./build/repo_search ./scripts/bench_search.sh corpus/fastapi "router"
```

Environment variables:

| Var    | Default                       | Purpose                    |
|--------|-------------------------------|----------------------------|
| `RUNS` | `7`                           | Number of iterations       |
| `BIN`  | `./build/debug/repo_search`   | Path to `repo_search` exe  |

For stable numbers, benchmark a Release build and pin `RUNS>=15`.

---

## Static Analysis

clang-tidy configuration is in `.clang-tidy`.  Requires a debug build first
(for `compile_commands.json`):

```bash
./scripts/build_debug.sh   # also creates compile_commands.json symlink
./scripts/lint.sh          # advisory — findings printed, non-blocking
```

To override `clang-tidy` version:
```bash
CLANG_TIDY=clang-tidy-15 ./scripts/lint.sh
```

---

## Pre-commit workflow

Run these commands before every commit or PR:

```bash
./scripts/format.sh         # 1. auto-format
./scripts/check_format.sh   # 2. verify clean (must pass)
./scripts/build_debug.sh    # 3. debug build + tests (must pass)
./scripts/lint.sh           # 4. static analysis (advisory)
```

---

## CI

GitHub Actions runs on every push and pull request to `main`:

| Step | Failure behaviour |
|---|---|
| Format check | Hard failure — blocks merge |
| Build (Debug) | Hard failure — blocks merge |
| Tests (CTest) | Hard failure — blocks merge |
| Lint (clang-tidy) | Advisory — shown but non-blocking |

Workflow definition: `.github/workflows/ci.yml`

---

## LLM / GraphRAG direction

Current scope:
- This project is a standalone C++ retrieval engine (parse -> index -> graph -> rank).
- It does **not** currently use LangChain, GraphChain, or GraphRAG.

Possible future architecture:
- Keep `repo_search` as the fast retrieval core.
- Add a thin service layer (for example, Python) that calls `repo_search`.
- Optionally integrate LangChain/GraphRAG in that outer layer for answer synthesis,
  tool orchestration, and multi-step reasoning.

This keeps the hot path low-latency while still allowing an LLM-powered UX on top.

---

## Project structure

```
src/
  tokenizer.{h,cpp}   — ASCII tokenizer (no allocations in hot path)
  parser.{h,cpp}      — file reader + Python import extractor
  main.cpp            — CLI driver

tests/
  test_tokenizer.cpp  — unit tests (hand-rolled runner, no framework)

scripts/
  format.sh           — apply clang-format
  check_format.sh     — verify formatting (used by CI)
  lint.sh             — run clang-tidy
  build_debug.sh      — debug build + tests + compile_commands.json symlink

.clang-format         — formatting rules
.clang-tidy           — static analysis rules
.github/workflows/    — CI configuration
```
