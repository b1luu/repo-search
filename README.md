# repo-search

A low-latency, dependency-aware code search engine written in C++20.

`repo_search` recursively indexes Python and JS/TS source files, builds a
language-aware intra-repository dependency graph, and ranks results using both
lexical similarity and graph structure.

Current language support:
- Python: `.py`, `.pyi`
- JavaScript / TypeScript: `.js`, `.jsx`, `.mjs`, `.cjs`, `.ts`, `.tsx`

Python graph resolution preserves dotted module paths and resolves relative
imports against the importing file's package path. JS/TS graph resolution
handles relative specifiers with extension and `index.*` probing.

## What It Does

Pipeline:
1. Parse supported source files under a directory
2. Tokenize file contents for lexical indexing
3. Extract import/module references
4. Build an inverted index plus an intra-corpus dependency graph
5. Score query results with TF-IDF and optional 1-hop graph expansion

Search output is file-level. Results include direct lexical matches plus
structurally related neighbors when graph expansion is enabled.

## Building

Release:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/repo_search <directory> <query>
```

Debug with ASan/UBSan:
```bash
./scripts/build_debug.sh
# Binary: build/debug/repo_search
```

## CLI

```bash
./build/repo_search [--top-k N] [--alpha F] <directory> <query>
```

Options:
- `--top-k N`: number of results to return, default `10`
- `--alpha F`: graph expansion weight in `[0,1]`, default `0.15`

Example:
```bash
./build/repo_search --top-k 5 --alpha 0.2 tests/fixtures/bench_corpus "payment refund"
```

## Testing

The test suite is registered with CTest:

```bash
ctest --test-dir build/debug --output-on-failure
```

Individual test executables are built under `build/debug/tests/`:
- `rs_tests_parser`
- `rs_tests_tokenizer`
- `rs_tests_indexer`
- `rs_tests_search`
- `rs_tests_integration`
- `rs_tests_relevance`

For example:
```bash
./build/debug/tests/rs_tests_relevance
```

Coverage areas:
- Parser unit tests for Python and JS/TS import extraction
- Tokenizer and indexer unit tests
- Search behavior tests
- Integration tests for graph construction and end-to-end ranking
- Relevance regression tests on committed fixture corpora

## Benchmarking

The repo includes a small committed benchmark fixture corpus under
`tests/fixtures/bench_corpus`.

Run the benchmark harness:
```bash
RUNS=7 WARMUP=2 ./scripts/bench_search.sh \
  --csv bench-results.csv \
  tests/fixtures/bench_corpus \
  "payment refund"
```

The harness reports:
- per-run parse, index, graph, and search timings
- min / median / p95 for search latency
- median parse / index / graph timings

Useful overrides:
- `RUNS`: number of measured runs, default `7`
- `WARMUP`: warm-up runs excluded from reporting, default `1`
- `BIN`: path to the binary, default `./build/debug/repo_search`

For stable numbers, use a Release build and increase `RUNS`.

## Formatting And Linting

Format all source files:
```bash
./scripts/format.sh
```

Check formatting without modifying files:
```bash
./scripts/check_format.sh
```

Run clang-tidy after a debug build:
```bash
./scripts/build_debug.sh
./scripts/lint.sh
```

If needed, override tool paths:
```bash
CLANG_FORMAT=/path/to/clang-format ./scripts/check_format.sh
CLANG_TIDY=/path/to/clang-tidy ./scripts/lint.sh
```

## Recommended Local Checks

Before opening a PR:
```bash
./scripts/check_format.sh
./scripts/build_debug.sh
./build/debug/tests/rs_tests_relevance
./scripts/lint.sh
```

`check_format.sh` and the debug build plus CTest are hard CI gates. Lint and
benchmarking are advisory.

## CI

GitHub Actions runs on pushes and pull requests to `main`.

Current workflow behavior:
- formatting check: blocking
- debug build: blocking
- CTest suite: blocking
- clang-tidy: advisory
- benchmark harness on committed fixture corpus: advisory

Workflow definition: `.github/workflows/ci.yml`

## Design Notes

Tokenizer:
- lowercases ASCII in place
- splits on non-alphanumeric boundaries
- drops single-character tokens

Python dependency graph:
- preserves absolute dotted imports such as `pkg.sub.mod`
- preserves relative imports such as `.utils` and `..core`
- resolves module paths from repository directory structure
- treats `__init__.py` as the package module

JS/TS dependency graph:
- extracts static `import`, `export ... from`, `require(...)`, and dynamic
  `import("...")` specifiers
- resolves only relative specifiers inside the indexed corpus
- drops bare package imports from the graph

Known limitations:
- no `sys.path`, `PYTHONPATH`, or virtualenv-aware Python resolution
- no support for dynamic Python import mechanisms such as `importlib`
- no `tsconfig` path aliases or `package.json` export resolution
- graph edges are intentionally limited to files inside the indexed corpus

## Project Structure

```text
src/
  main.cpp            CLI driver
  tokenizer.{h,cpp}   lexical tokenization
  parser.{h,cpp}      file parsing + import extraction
  indexer.{h,cpp}     vocabulary, postings, and index construction
  graph_builder.{h,cpp}
                      dependency graph construction
  search_engine.{h,cpp}
                      ranking and graph-assisted retrieval

tests/
  test_parser.cpp
  test_tokenizer.cpp
  test_indexer.cpp
  test_search.cpp
  test_integration.cpp
  test_relevance.cpp
  fixtures/bench_corpus/
                      committed mixed-language benchmark corpus

scripts/
  build_debug.sh
  format.sh
  check_format.sh
  lint.sh
  bench_search.sh
```
