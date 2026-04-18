# repo-search

A dependency-aware code search engine written in C++20.

`repo_search` indexes a Python + JS/TS codebase, builds an in-memory dependency
graph from its imports, and ranks files by combining lexical relevance
(TF-IDF) with 1-hop graph expansion — so a file can surface not just because
it matches the query, but because it's structurally adjacent to files that do.

The whole pipeline — parse → tokenize → index → graph → search — runs
entirely in-process with no external dependencies beyond libc++.

---

## Highlights

- **Sub-millisecond query latency** on a 1,125-file Python codebase (FastAPI)
  after a one-time ~430 ms parse+index build.
- **Dependency-aware ranking.** Python imports resolve against repository
  layout (dotted paths, `__init__.py` packages, relative imports). JS/TS
  imports resolve relative specifiers with extension and `index.*` probing.
- **Zero-dependency C++20.** Single static binary. Uses only the standard
  library. No Boost, no regex engine, no third-party parser.
- **Modular pipeline.** Each pipeline stage — source discovery, import
  extraction, language-specific module resolution, indexing, graph
  construction, ranking — lives in its own translation unit with a narrow
  public surface.
- **Covered by 6 test suites** (parser, tokenizer, indexer, search,
  integration, relevance) plus a committed mixed-language benchmark corpus.

---

## Example

```
$ ./build/repo_search corpus/fastapi "dependency injection"
Parse:  1125 files  (406.0 ms)
Index:  1125 docs, 4004 terms  (20.2 ms)
Graph:  1588 intra-corpus edges  (5.5 ms)

Search: "dependency injection"  (48.4 µs)

Top results:
  [0.4185] corpus/fastapi/fastapi/__init__.py
  [0.4158] corpus/fastapi/tests/test_dependency_partial.py
  [0.4123] corpus/fastapi/tests/test_dependency_class.py
  [0.3401] corpus/fastapi/docs_src/dependencies/tutorial008_py310.py
  [0.3150] corpus/fastapi/fastapi/testclient.py
```

Sub-50 µs search, including graph reranking, over 1,125 indexed files.

---

## How It Works

```
   source tree
       │
       ▼
 ┌───────────────┐
 │  discovery    │   deterministic walk, language classification
 └──────┬────────┘
        ▼
 ┌───────────────┐
 │  parse        │   read file → extract imports → tokenize
 └──────┬────────┘
        ▼
 ┌───────────────┐
 │  index        │   vocabulary, postings, TF normalization
 └──────┬────────┘
        ▼
 ┌───────────────┐
 │  graph        │   resolve imports to file_ids → adjacency lists
 └──────┬────────┘
        ▼
 ┌───────────────┐
 │  search       │   TF-IDF scoring → 1-hop graph expansion → top-k
 └───────────────┘
```

### Scoring

```
lex_score(f)    = Σ_t  tf(t, f) · idf(t)
idf(t)          = log((N + 1) / (df(t) + 1))            # smoothed
graph_bonus(f)  = α · Σ_{n ∈ adj(f) ∪ radj(f)}  lex_score(n)
final_score(f)  = lex_score(f) + graph_bonus(f)
```

- `adj[f]` = files that `f` imports (`f` uses these)
- `radj[f]` = files that import `f` (these use `f`)
- `α = 0` disables graph expansion entirely
- Files with `lex_score = 0` but `graph_bonus > 0` surface as structurally
  related matches, a capability a pure lexical search cannot provide

### Language-aware resolution

**Python**
- dotted imports (`pkg.sub.mod`) resolved against repository directory layout
- relative imports (`.utils`, `..core`) resolved from the importing module's
  package path
- `__init__.py` treated as its parent package's module

**JavaScript / TypeScript**
- lightweight state machine extracts static `import`, `export ... from`,
  `require(...)`, and dynamic `import(...)` specifiers
- relative specifiers resolved with extension precedence
  (`.ts > .tsx > .js > .jsx > .mjs > .cjs`), then `index.*` probing,
  then raw path
- bare specifiers (`"react"`) are dropped (no `node_modules`, no `tsconfig`
  paths, no `package.json` exports)

---

## Building

**Release (recommended for real corpora):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/repo_search <directory> <query>
```

**Debug with AddressSanitizer + UBSan:**
```bash
./scripts/build_debug.sh
# binary: build/debug/repo_search
```

Requires a C++20 compiler (AppleClang, Clang, or GCC). No third-party
dependencies.

---

## CLI

```
repo_search [--top-k N] [--alpha F] <directory> <query>
```

| Flag | Default | Description |
| --- | --- | --- |
| `--top-k N` | `10` | number of results to return |
| `--alpha F` | `0.15` | graph expansion weight in `[0, 1]` (0 disables) |

```bash
./build/repo_search --top-k 5 --alpha 0.2 corpus/fastapi "dependency injection"
```

---

## Testing

All tests run under CTest:

```bash
./scripts/build_debug.sh      # builds and runs all 6 suites
# or, if already built:
ctest --test-dir build/debug --output-on-failure
```

| Suite | What it covers |
| --- | --- |
| `parser` | Python + JS/TS import extraction, source-file discovery, extension filtering |
| `tokenizer` | ASCII lowercasing, token boundary rules |
| `indexer` | Vocabulary interning, postings layout, TF normalization |
| `search` | Query resolution, TF-IDF ranking, deterministic tie-breaking |
| `integration` | End-to-end pipeline on a tiny corpus fixture |
| `relevance` | Regression tests: query → expected file(s) on a committed fixture |

Deterministic ordering is a first-class invariant: source files are sorted
before `file_id` assignment, and ranking ties break on `(score, path, file_id)`
so output is reproducible across platforms and runs.

---

## Benchmarking

A committed mixed-language fixture corpus lives under
`tests/fixtures/bench_corpus`.

```bash
RUNS=7 WARMUP=2 ./scripts/bench_search.sh \
  --csv bench-results.csv \
  tests/fixtures/bench_corpus \
  "payment refund"
```

The harness reports:
- per-run parse / index / graph / search timings
- min / median / p95 for search latency
- median parse / index / graph timings

Overrides: `RUNS`, `WARMUP`, `BIN` (defaults to `./build/debug/repo_search`).
Use a Release build for stable numbers.

---

## Project Structure

The codebase is organized into small, single-purpose translation units.
Language policy, filesystem access, and ranking math each live in their own
module — orchestration files are thin.

```
src/
  main.cpp               CLI driver, pipeline timing
  cli.{h,cpp}            argv parsing

  source_path.h          extension → language classification (single source of truth)
  source_discovery.{h,cpp}  deterministic recursive file walk
  file_reader.{h,cpp}    bulk file read
  parsed_file_builder.{h,cpp}  per-file pipeline coordinator
  parser.{h,cpp}         thin orchestrator: discover → read → extract → tokenize

  import_extractor.{h,cpp}  parser-side language dispatch
  python_imports.{h,cpp}    Python import statement scanner
  js_imports.{h,cpp}        JS/TS import/require state machine

  tokenizer.{h,cpp}      ASCII tokenization (shared view + owned variants)
  ascii_utils.{h,cpp}    ASCII lowercase
  collection_utils.{h,cpp}  stable-order dedup

  indexer.{h,cpp}        Vocabulary, postings, build_index()

  python_modules.{h,cpp} Python module-path → file_id resolution
  js_modules.{h,cpp}     JS/TS relative-specifier → file_id resolution
  import_resolver.{h,cpp}  graph-side language dispatch
  graph_builder.{h,cpp}  Graph type + build_graph()

  search_engine.{h,cpp}  resolve_query_terms → score_lexical → expand_via_graph → collect_top_k

tests/
  test_check.h           shared CHECK / CHECK_EQ / CHECK_MSG macros
  test_{parser,tokenizer,indexer,search,integration,relevance}.cpp
  fixtures/bench_corpus/ committed mixed-language benchmark corpus

scripts/
  build_debug.sh  format.sh  check_format.sh  lint.sh  bench_search.sh
```

---

## Design Notes & Trade-offs

**What it indexes.** Source files only, by extension (`.py`, `.pyi`, `.ts`,
`.tsx`, `.js`, `.jsx`, `.mjs`, `.cjs`). Binary files and unsupported
extensions are ignored.

**What it is not.** This is not a full compiler front-end. There is no AST,
no scope analysis, no type information. Import resolution is structural and
repository-local.

**Known limitations.**
- no `sys.path` / `PYTHONPATH` / virtualenv-aware Python resolution
- no dynamic Python imports (`importlib`, `__import__`)
- no `tsconfig` path aliases or `package.json` export conditions
- no `node_modules` traversal — bare specifiers are intentionally dropped
- graph edges are intentionally limited to files inside the indexed corpus

**Why C++20.** Parse/index throughput dominates cold-start cost on large
repositories. Move semantics, `string_view`, transparent hashing, and
`std::filesystem::lexically_normal` let the hot paths avoid redundant
allocations and copies.

---

## Formatting And Linting

```bash
./scripts/format.sh         # format all source files
./scripts/check_format.sh   # CI formatting gate
./scripts/lint.sh           # clang-tidy (advisory)
```

Override tool paths if needed: `CLANG_FORMAT=... ./scripts/check_format.sh`,
`CLANG_TIDY=... ./scripts/lint.sh`.

## Recommended Local Checks

Before opening a PR:
```bash
./scripts/check_format.sh
./scripts/build_debug.sh     # builds + runs full test suite
./scripts/lint.sh            # advisory
```

## CI

GitHub Actions runs on pushes and pull requests to `main` — see
`.github/workflows/ci.yml`.

| Check | Status |
| --- | --- |
| formatting (`clang-format`) | blocking |
| debug build with ASan/UBSan | blocking |
| full CTest suite | blocking |
| clang-tidy | advisory |
| benchmark harness on committed corpus | advisory |
