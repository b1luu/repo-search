#include "graph_builder.h"
#include "indexer.h"
#include "parser.h"
#include "search_engine.h"
#include "tokenizer.h"

#include "test_check.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_skipped = 0;

// ---------------------------------------------------------------------------
// Tiny corpus content
//
// Design invariants (verified by test_webhook_postings_exactly_one_file):
//   "webhook" appears ~10 times in handler.py and ZERO times in all other files.
//   router.py contains `from handler import receive_event` → graph edge to handler.
//   "authenticate" appears ~6 times in auth.py and ZERO times elsewhere.
// ---------------------------------------------------------------------------

static constexpr const char* HANDLER_PY = R"py(
"""Handles webhook events from external services."""

ENDPOINT = "/api/webhook"
MAX_RETRIES = 3

def receive_event(payload, headers):
    """Receive and process an inbound webhook notification."""
    webhook_id = headers.get("webhook-id")
    if not verify_webhook_signature(payload):
        raise ValueError("invalid webhook signature")
    return store_webhook_event(payload, webhook_id)

def verify_webhook_signature(payload):
    return True

def store_webhook_event(payload, webhook_id):
    return payload
)py";

static constexpr const char* AUTH_PY = R"py(
"""Authentication module for user credential management."""

AUTHENTICATION_REQUIRED = True

class TokenAuthenticator:
    """Handles user authentication via token-based authenticate flow."""

    def authenticate(self, username, password):
        """Authenticate a user with username and password credentials."""
        if not self.can_authenticate(username):
            raise PermissionError("cannot authenticate")
        token = self.generate_token(username, password)
        return self.validate_token(token)

    def can_authenticate(self, username):
        return username is not None

    def generate_token(self, username, password):
        return hash_credentials(username, password)

    def validate_token(self, token):
        return token is not None
)py";

// Intentionally: no "webhook" token.
// `from handler import receive_event` → graph_builder resolves "handler" → handler.py.
static constexpr const char* ROUTER_PY = R"py(
"""HTTP request routing module."""
from handler import receive_event

class RequestRouter:
    """Routes incoming HTTP requests to appropriate handlers."""

    def dispatch(self, path, payload, headers):
        """Dispatch a request based on its path."""
        if path.startswith("/api"):
            return receive_event(payload, headers)
        return None

    def route(self, request):
        """Route a request object to the correct handler."""
        return self.dispatch(request.path, request.body, request.headers)
)py";

static constexpr const char* MODELS_PY = R"py(
"""Database models and schema definitions."""

class EventModel:
    """Represents an event record in the database."""

    table_name = "events"

    def __init__(self, event_id, payload, timestamp):
        self.event_id  = event_id
        self.payload   = payload
        self.timestamp = timestamp

    def save(self):
        """Persist this event model to the database."""
        return database_insert(self.table_name, self.__dict__)

    def to_dict(self):
        """Convert this event model to a plain dictionary."""
        return {"id": self.event_id, "payload": self.payload}
)py";

static constexpr const char* UTILS_PY = R"py(
"""Utility functions for serialization and schema validation."""

def serialize(data):
    """Serialize data to a string format."""
    return json_encode(data)

def deserialize(raw):
    """Deserialize a raw string back to a Python object."""
    return json_decode(raw)

def validate_schema(data, schema):
    """Validate data keys against a required schema definition."""
    for key in schema:
        if key not in data:
            raise ValueError(f"missing required key: {key}")
    return True
)py";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void write_file(const fs::path& p, const char* content) {
    std::ofstream f(p);
    f << content;
}

// Returns file_id for a file whose path ends with `suffix`, or ~0u if absent.
static uint32_t find_fid(const rs::Index& idx, std::string_view suffix) {
    for (uint32_t i = 0; i < idx.num_docs; ++i) {
        if (idx.paths[i].ends_with(suffix))
            return i;
    }
    return ~uint32_t{0};
}

// Returns true iff graph.adj[from] contains `to`.
static bool has_edge(const rs::Graph& g, uint32_t from, uint32_t to) {
    if (from >= g.adj.size())
        return false;
    auto const& ns = g.adj[from];
    return std::find(ns.begin(), ns.end(), to) != ns.end();
}

// ---------------------------------------------------------------------------
// Naive O(N × D × Q) linear scan — simulates search without an inverted index.
// Token data is snapshotted from ParsedFiles before build_index() consumes them.
// ---------------------------------------------------------------------------

struct NaiveDoc {
    std::string path;
    std::vector<std::string> tokens; // owned copies of token_views
};

static std::vector<NaiveDoc> snapshot(const std::vector<rs::ParsedFile>& files) {
    std::vector<NaiveDoc> docs;
    docs.reserve(files.size());
    for (auto const& pf : files) {
        NaiveDoc nd;
        nd.path = pf.path;
        nd.tokens.reserve(pf.token_views.size());
        for (auto const& tv : pf.token_views)
            nd.tokens.emplace_back(tv);
        docs.push_back(std::move(nd));
    }
    return docs;
}

// Returns the path of the file with the highest raw token-match count.
static std::string naive_top(const std::vector<NaiveDoc>& docs, std::string_view query) {
    auto qtoks = rs::tokenize_owned(query);
    std::string best_path;
    float best = 0.0f;
    for (auto const& doc : docs) {
        float score = 0.0f;
        for (auto const& qt : qtoks)
            for (auto const& tok : doc.tokens)
                if (tok == qt)
                    score += 1.0f;
        if (score > best) {
            best = score;
            best_path = doc.path;
        }
    }
    return best_path;
}

// Measures average latency of the naive scan over `reps` iterations (µs).
// Uses a volatile sink to prevent the compiler from eliding the work.
static double naive_latency(const std::vector<NaiveDoc>& docs, std::string_view query, int reps) {
    auto t0 = std::chrono::steady_clock::now();
    float sink = 0.0f;
    for (int i = 0; i < reps; ++i) {
        auto qtoks = rs::tokenize_owned(query);
        for (auto const& doc : docs)
            for (auto const& qt : qtoks)
                for (auto const& tok : doc.tokens)
                    if (tok == qt)
                        sink += 1.0f;
    }
    auto t1 = std::chrono::steady_clock::now();
    // Use sink so the compiler keeps the loop body.
    if (sink < 0.0f)
        std::printf("(sink)\n");
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / static_cast<double>(reps);
}

// Measures average indexed search latency over `reps` iterations (µs).
static double indexed_latency(const rs::Index& idx, const rs::Graph& graph, std::string_view query,
                              int reps) {
    auto t0 = std::chrono::steady_clock::now();
    std::size_t sink = 0;
    for (int i = 0; i < reps; ++i)
        sink += rs::search(query, idx, graph).size();
    auto t1 = std::chrono::steady_clock::now();
    if (sink == 0)
        std::printf("(no results)\n");
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / static_cast<double>(reps);
}

// ---------------------------------------------------------------------------
// ---- Tiny corpus fixture ---------------------------------------------------
// ---------------------------------------------------------------------------

struct TinyCorpus {
    rs::Index idx;
    rs::Graph graph;
    std::vector<NaiveDoc> naive;
    fs::path dir;
};

static TinyCorpus make_tiny_corpus() {
    auto dir = fs::temp_directory_path() / "rs_integration_tiny";
    fs::create_directories(dir);

    write_file(dir / "handler.py", HANDLER_PY);
    write_file(dir / "auth.py", AUTH_PY);
    write_file(dir / "router.py", ROUTER_PY);
    write_file(dir / "models.py", MODELS_PY);
    write_file(dir / "utils.py", UTILS_PY);

    auto files = rs::parse_directory(dir);
    auto naive = snapshot(files); // snapshot before build_index consumes
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);

    return {std::move(idx), std::move(graph), std::move(naive), dir};
}

// ---------------------------------------------------------------------------
// ---- Tiny corpus tests -----------------------------------------------------
// ---------------------------------------------------------------------------

// "webhook" must exist in exactly one file (handler.py) and be absent from all others.
static void test_webhook_postings_exactly_one_file(const TinyCorpus& c) {
    const uint32_t webhook_tid = c.idx.vocab.find("webhook");
    CHECK(webhook_tid != ~uint32_t{0}); // term must be in vocab

    // Exactly one file contains "webhook".
    CHECK_EQ(c.idx.postings[webhook_tid].size(), 1u);

    // That file must be handler.py.
    const uint32_t handler_fid = find_fid(c.idx, "handler.py");
    CHECK(handler_fid != ~uint32_t{0});
    CHECK_EQ(c.idx.postings[webhook_tid][0].file_id, handler_fid);
}

// Lexical search must rank handler.py first — it has the highest TF for "webhook".
static void test_webhook_query_handler_ranks_first(const TinyCorpus& c) {
    auto results = rs::search("webhook", c.idx, c.graph);
    CHECK(!results.empty());
    CHECK(results[0].path.ends_with("handler.py"));
    CHECK(results[0].score > 0.0f);
}

// router.py imports handler.py → it must appear in results via 1-hop graph expansion.
// It has zero lexical score for "webhook" but receives a graph bonus from handler.
static void test_graph_expands_importer_into_results(const TinyCorpus& c) {
    rs::SearchParams params;
    params.top_k = 10;
    params.graph_alpha = 0.15f;

    auto results = rs::search("webhook", c.idx, c.graph, params);

    bool found_router = false;
    for (auto const& r : results) {
        if (r.path.ends_with("router.py")) {
            found_router = true;
            break;
        }
    }
    CHECK(found_router);

    // router.py must NOT rank above handler.py (it has only a graph bonus).
    if (results.size() >= 2) {
        CHECK(results[0].path.ends_with("handler.py") || results[0].score >= results[1].score);
    }
}

// With graph expansion disabled, router.py (zero lexical match) must be absent.
static void test_graph_disabled_excludes_pure_graph_files(const TinyCorpus& c) {
    rs::SearchParams params;
    params.top_k = 10;
    params.graph_alpha = 0.0f;

    auto results = rs::search("webhook", c.idx, c.graph, params);
    for (auto const& r : results) {
        CHECK(!r.path.ends_with("router.py"));
    }
}

// Indexed and naive scan must agree on the top result for "webhook".
static void test_indexed_and_naive_agree_on_top_result(const TinyCorpus& c) {
    auto results = rs::search("webhook", c.idx, c.graph, {.top_k = 1, .graph_alpha = 0.0f});
    CHECK(!results.empty());

    const std::string naive_best = naive_top(c.naive, "webhook");
    // Both must identify handler.py as the best match.
    CHECK(results[0].path.ends_with("handler.py"));
    CHECK(naive_best.ends_with("handler.py"));
}

// "authenticate" appears many times in auth.py and zero times elsewhere.
static void test_auth_term_single_file_and_ranks_first(const TinyCorpus& c) {
    const uint32_t auth_tid = c.idx.vocab.find("authenticate");
    CHECK(auth_tid != ~uint32_t{0});

    // auth.py must be the only file with "authenticate".
    CHECK_EQ(c.idx.postings[auth_tid].size(), 1u);

    auto results = rs::search("authenticate", c.idx, c.graph);
    CHECK(!results.empty());
    CHECK(results[0].path.ends_with("auth.py"));
}

// ---------------------------------------------------------------------------
// ---- Python package corpus -------------------------------------------------
//
// Exercises full dotted module-path resolution and relative imports.
//
// Layout:
//   app.py               import pkg.mod; from pkg.sub import func
//   pkg/__init__.py       (empty)
//   pkg/mod.py            from .sub import func
//   pkg/sub.py            (no imports)
//   lib/utils.py          (no imports)
//   lib/core.py           from .utils import helper
//   pkg/deep/__init__.py  from ..mod import thing
//
// Expected module map:
//   app.py              → "app"
//   pkg/__init__.py     → "pkg"
//   pkg/mod.py          → "pkg.mod"
//   pkg/sub.py          → "pkg.sub"
//   lib/utils.py        → "lib.utils"
//   lib/core.py         → "lib.core"
//   pkg/deep/__init__.py → "pkg.deep"
//
// Expected edges (5 total):
//   app        → pkg.mod       (via "pkg.mod")
//   app        → pkg.sub       (via "pkg.sub")
//   pkg.mod    → pkg.sub       (via ".sub" resolved from pkg.mod)
//   lib.core   → lib.utils     (via ".utils" resolved from lib.core)
//   pkg.deep   → pkg.mod       (via "..mod" resolved from pkg.deep __init__)
// ---------------------------------------------------------------------------

static constexpr const char* PY_APP = R"py(
import pkg.mod
from pkg.sub import func
)py";

static constexpr const char* PY_PKG_INIT = R"py(
"""Package init."""
)py";

static constexpr const char* PY_PKG_MOD = R"py(
from .sub import func

def thing():
    return func()
)py";

static constexpr const char* PY_PKG_SUB = R"py(
def func():
    return 42
)py";

static constexpr const char* PY_LIB_UTILS = R"py(
def helper():
    return "help"
)py";

static constexpr const char* PY_LIB_CORE = R"py(
from .utils import helper

def run():
    return helper()
)py";

static constexpr const char* PY_PKG_DEEP_INIT = R"py(
from ..mod import thing
)py";

struct PyPkgCorpus {
    rs::Index idx;
    rs::Graph graph;
    fs::path dir;
};

static PyPkgCorpus make_py_pkg_corpus() {
    auto dir = fs::temp_directory_path() / "rs_integration_py_pkg";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::create_directories(dir / "pkg");
    fs::create_directories(dir / "pkg" / "deep");
    fs::create_directories(dir / "lib");

    write_file(dir / "app.py", PY_APP);
    write_file(dir / "pkg" / "__init__.py", PY_PKG_INIT);
    write_file(dir / "pkg" / "mod.py", PY_PKG_MOD);
    write_file(dir / "pkg" / "sub.py", PY_PKG_SUB);
    write_file(dir / "lib" / "utils.py", PY_LIB_UTILS);
    write_file(dir / "lib" / "core.py", PY_LIB_CORE);
    write_file(dir / "pkg" / "deep" / "__init__.py", PY_PKG_DEEP_INIT);

    auto files = rs::parse_directory(dir);
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph), dir};
}

// All 7 Python files must be parsed.
static void test_py_pkg_all_files_parsed(const PyPkgCorpus& c) {
    CHECK_EQ(c.idx.num_docs, 7u);
}

// Absolute dotted import: app.py → pkg/mod.py (via "pkg.mod")
static void test_py_pkg_absolute_dotted_import(const PyPkgCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.py");
    const uint32_t mod = find_fid(c.idx, "pkg/mod.py");
    CHECK(app != ~uint32_t{0});
    CHECK(mod != ~uint32_t{0});
    CHECK(has_edge(c.graph, app, mod));
}

// Absolute dotted from-import: app.py → pkg/sub.py (via "pkg.sub")
static void test_py_pkg_absolute_from_import(const PyPkgCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.py");
    const uint32_t sub = find_fid(c.idx, "pkg/sub.py");
    CHECK(app != ~uint32_t{0});
    CHECK(sub != ~uint32_t{0});
    CHECK(has_edge(c.graph, app, sub));
}

// Single-dot relative: pkg/mod.py → pkg/sub.py (via ".sub")
static void test_py_pkg_single_dot_relative(const PyPkgCorpus& c) {
    const uint32_t mod = find_fid(c.idx, "pkg/mod.py");
    const uint32_t sub = find_fid(c.idx, "pkg/sub.py");
    CHECK(mod != ~uint32_t{0});
    CHECK(sub != ~uint32_t{0});
    CHECK(has_edge(c.graph, mod, sub));
}

// Single-dot relative in different package: lib/core.py → lib/utils.py
static void test_py_pkg_relative_cross_sibling(const PyPkgCorpus& c) {
    const uint32_t core = find_fid(c.idx, "lib/core.py");
    const uint32_t utils = find_fid(c.idx, "lib/utils.py");
    CHECK(core != ~uint32_t{0});
    CHECK(utils != ~uint32_t{0});
    CHECK(has_edge(c.graph, core, utils));
}

// Double-dot relative from __init__.py: pkg/deep/__init__.py → pkg/mod.py
static void test_py_pkg_double_dot_from_init(const PyPkgCorpus& c) {
    const uint32_t deep = find_fid(c.idx, "pkg/deep/__init__.py");
    const uint32_t mod = find_fid(c.idx, "pkg/mod.py");
    CHECK(deep != ~uint32_t{0});
    CHECK(mod != ~uint32_t{0});
    CHECK(has_edge(c.graph, deep, mod));
}

// No collision: lib/utils.py and pkg/sub.py are distinct modules.
// "pkg.sub" must NOT resolve to lib/utils.py.
static void test_py_pkg_no_cross_package_collision(const PyPkgCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.py");
    const uint32_t lib_utils = find_fid(c.idx, "lib/utils.py");
    CHECK(app != ~uint32_t{0});
    CHECK(lib_utils != ~uint32_t{0});
    CHECK(!has_edge(c.graph, app, lib_utils));
}

// Total edge count must be exactly 5.
static void test_py_pkg_total_edges(const PyPkgCorpus& c) {
    std::size_t total = 0;
    for (auto const& nbrs : c.graph.adj)
        total += nbrs.size();
    CHECK_EQ(total, 5u);
}

// ---------------------------------------------------------------------------
// ---- FastAPI corpus --------------------------------------------------------
// ---------------------------------------------------------------------------

// Locate the FastAPI source directory.  Accepts RS_FASTAPI_PATH env override;
// otherwise shells out to Python to ask where fastapi is installed.
static fs::path find_fastapi() {
    if (const char* env = std::getenv("RS_FASTAPI_PATH")) {
        fs::path p(env);
        if (fs::exists(p))
            return p;
    }
    FILE* fp = popen("python3 -c 'import fastapi,os;print(os.path.dirname(fastapi.__file__))' "
                     "2>/dev/null",
                     "r");
    if (!fp)
        return {};
    char buf[4096] = {};
    if (!std::fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        return {};
    }
    pclose(fp);
    std::string path(buf);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
        path.pop_back();
    if (path.empty())
        return {};
    fs::path result(path);
    return fs::exists(result) ? result : fs::path{};
}

// background.py contains "background" 10× in 200 tokens (tf ≈ 0.050).
// The next highest is __init__.py with tf ≈ 0.007 — a clear 7× margin.
// This makes background.py the definitive top result for the "background" query.
static void test_fastapi_background_file_ranks_first(const rs::Index& idx, const rs::Graph& graph) {
    auto results = rs::search("background", idx, graph);
    CHECK(!results.empty());
    CHECK(results[0].path.ends_with("background.py"));
}

// Indexed search must be strictly faster than the naive O(N×D×Q) scan.
// FastAPI corpus: 48 files, ~74k total tokens, ~39 "background" postings.
// Expected speedup: >100×; threshold used: 5× (conservative).
static void test_fastapi_indexed_faster_than_naive(const rs::Index& idx, const rs::Graph& graph,
                                                   const std::vector<NaiveDoc>& naive) {
    constexpr int REPS = 500;
    const double indexed_us = indexed_latency(idx, graph, "background", REPS);
    const double naive_us = naive_latency(naive, "background", REPS);

    std::printf("  indexed: %.2f µs/call   naive: %.2f µs/call   speedup: %.1fx\n", indexed_us,
                naive_us, naive_us / indexed_us);

    // Indexed must be faster.  For 48 files × 74k total tokens vs. 45 postings,
    // the speedup should be >50×.  We use a conservative 5× threshold.
    CHECK(indexed_us < naive_us / 5.0);
}

// ---------------------------------------------------------------------------
// ---- TS/JS corpus fixture --------------------------------------------------
//
// A tiny hand-built TypeScript corpus exercising JS/TS import extraction and
// the graph builder's relative-path resolver.
//
// Layout:
//   app.tsx              imports "./utils", "./helpers/format", "./widgets", "react"
//   utils.ts             imports "./types"
//   types.ts             (no imports)
//   helpers/format.ts    imports "../utils"
//   widgets/index.ts     (no imports) — exercises directory-index resolution
//
// Expected intra-corpus edges (5 total):
//   app.tsx          → utils.ts
//   app.tsx          → helpers/format.ts
//   app.tsx          → widgets/index.ts
//   utils.ts         → types.ts
//   helpers/format.ts → utils.ts
//
// The bare specifier "react" must NOT become an edge — it is dropped because
// it does not resolve to a file in the corpus.
// ---------------------------------------------------------------------------

static constexpr const char* TS_APP = R"ts(
import { useMemo } from "react";
import { helper } from "./utils";
import { format } from "./helpers/format";
import { Widget } from "./widgets";

export function App() {
  return format(helper(Widget));
}
)ts";

static constexpr const char* TS_UTILS = R"ts(
import type { Thing } from "./types";

export function helper(w: Thing) {
  return w;
}
)ts";

static constexpr const char* TS_TYPES = R"ts(
export type Thing = { id: number; label: string };
)ts";

static constexpr const char* TS_HELPERS_FORMAT = R"ts(
import { helper } from "../utils";

export function format(x: unknown) {
  return helper(x as never);
}
)ts";

static constexpr const char* TS_WIDGETS_INDEX = R"ts(
export const Widget = { id: 1, label: "w" };
)ts";

struct TsCorpus {
    rs::Index idx;
    rs::Graph graph;
    fs::path dir;
};

static TsCorpus make_ts_corpus() {
    auto dir = fs::temp_directory_path() / "rs_integration_ts";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::create_directories(dir / "helpers");
    fs::create_directories(dir / "widgets");

    write_file(dir / "app.tsx", TS_APP);
    write_file(dir / "utils.ts", TS_UTILS);
    write_file(dir / "types.ts", TS_TYPES);
    write_file(dir / "helpers" / "format.ts", TS_HELPERS_FORMAT);
    write_file(dir / "widgets" / "index.ts", TS_WIDGETS_INDEX);

    auto files = rs::parse_directory(dir);
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph), dir};
}

// ---------------------------------------------------------------------------
// ---- TS/JS corpus tests ----------------------------------------------------
// ---------------------------------------------------------------------------

// All five expected files must be parsed.
static void test_ts_corpus_parses_all_files(const TsCorpus& c) {
    CHECK_EQ(c.idx.num_docs, 5u);
}

// Every expected edge must exist; the total edge count must be exactly 5.
static void test_ts_corpus_expected_edges_exist(const TsCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.tsx");
    const uint32_t utils = find_fid(c.idx, "utils.ts");
    const uint32_t types = find_fid(c.idx, "types.ts");
    const uint32_t format = find_fid(c.idx, "format.ts");
    const uint32_t widgets = find_fid(c.idx, "widgets/index.ts");

    CHECK(app != ~uint32_t{0});
    CHECK(utils != ~uint32_t{0});
    CHECK(types != ~uint32_t{0});
    CHECK(format != ~uint32_t{0});
    CHECK(widgets != ~uint32_t{0});

    // Each directed edge expected by the layout above.
    CHECK(has_edge(c.graph, app, utils));
    CHECK(has_edge(c.graph, app, format));
    CHECK(has_edge(c.graph, app, widgets));
    CHECK(has_edge(c.graph, utils, types));
    CHECK(has_edge(c.graph, format, utils));

    // And the total must match — no spurious edges from "react" or elsewhere.
    std::size_t total = 0;
    for (auto const& nbrs : c.graph.adj)
        total += nbrs.size();
    CHECK_EQ(total, 5u);
}

// The bare specifier "react" must NOT produce an edge in the graph,
// because "react" does not resolve to any file in the corpus.
static void test_ts_corpus_bare_import_not_resolved(const TsCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.tsx");
    CHECK(app != ~uint32_t{0});

    // app.tsx has 4 import specifiers but only 3 resolve to corpus files.
    CHECK_EQ(c.graph.adj[app].size(), 3u);
}

// `import { Widget } from "./widgets"` must resolve to widgets/index.ts
// via the directory-as-module (index.<ext>) convention.
static void test_ts_corpus_index_file_resolution(const TsCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.tsx");
    const uint32_t widgets = find_fid(c.idx, "widgets/index.ts");
    CHECK(app != ~uint32_t{0});
    CHECK(widgets != ~uint32_t{0});
    CHECK(has_edge(c.graph, app, widgets));
}

// ---------------------------------------------------------------------------
// ---- TS/JS hardening corpus ------------------------------------------------
//
// Exercises edge cases in resolve_js_relative + graph-level dedup:
//   - unresolved relative specifier produces no edge
//   - extension precedence (.ts wins over .js)
//   - lexical path normalization of `./a/../b`
//   - two distinct specifiers that resolve to the same file collapse to one
//     edge
//
// Layout:
//   app.ts   imports "./nowhere"    (unresolved)
//            imports "./util"       (util.ts vs util.js → .ts wins)
//            imports "./a/../b"     (normalizes to ./b → b.ts)
//            imports "./foo"        (→ foo.ts)
//            imports "./foo.ts"     (same file — graph dedups)
//   util.ts, util.js, foo.ts, b.ts  (no imports)
// ---------------------------------------------------------------------------

static constexpr const char* TS_HARDEN_APP = R"ts(
import { gone } from "./nowhere";
import { u } from "./util";
import { b } from "./a/../b";
import { foo1 } from "./foo";
import { foo2 } from "./foo.ts";
)ts";

static constexpr const char* TS_HARDEN_UTIL_TS = "export const u = 1;\n";
static constexpr const char* TS_HARDEN_UTIL_JS = "export const u = 2;\n";
static constexpr const char* TS_HARDEN_FOO = "export const foo1 = 1;\nexport const foo2 = 2;\n";
static constexpr const char* TS_HARDEN_B = "export const b = 1;\n";

struct TsHardenCorpus {
    rs::Index idx;
    rs::Graph graph;
    fs::path dir;
};

static TsHardenCorpus make_ts_harden_corpus() {
    auto dir = fs::temp_directory_path() / "rs_integration_ts_harden";
    fs::remove_all(dir);
    fs::create_directories(dir);

    write_file(dir / "app.ts", TS_HARDEN_APP);
    write_file(dir / "util.ts", TS_HARDEN_UTIL_TS);
    write_file(dir / "util.js", TS_HARDEN_UTIL_JS);
    write_file(dir / "foo.ts", TS_HARDEN_FOO);
    write_file(dir / "b.ts", TS_HARDEN_B);

    auto files = rs::parse_directory(dir);
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph), dir};
}

// Unresolved relative specifier must NOT create an edge; app has exactly
// three outgoing edges (util.ts, b.ts, foo.ts — dedup'd).
static void test_ts_harden_unresolved_makes_no_edge(const TsHardenCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.ts");
    CHECK(app != ~uint32_t{0});
    CHECK_EQ(c.graph.adj[app].size(), 3u);
}

// `./util` must resolve to util.ts, not util.js, because the resolver's
// extension list puts .ts before .js.
static void test_ts_harden_extension_precedence_ts_over_js(const TsHardenCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.ts");
    const uint32_t util_ts = find_fid(c.idx, "util.ts");
    const uint32_t util_js = find_fid(c.idx, "util.js");
    CHECK(app != ~uint32_t{0});
    CHECK(util_ts != ~uint32_t{0});
    CHECK(util_js != ~uint32_t{0});
    CHECK(has_edge(c.graph, app, util_ts));
    CHECK(!has_edge(c.graph, app, util_js));
}

// `./a/../b` must normalize to `./b` and resolve to b.ts.
static void test_ts_harden_path_normalization_with_dotdot(const TsHardenCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.ts");
    const uint32_t b = find_fid(c.idx, "b.ts");
    CHECK(app != ~uint32_t{0});
    CHECK(b != ~uint32_t{0});
    CHECK(has_edge(c.graph, app, b));
}

// `./foo` and `./foo.ts` both resolve to foo.ts — the graph must dedupe
// this into a single edge.
static void test_ts_harden_dedup_across_specifiers(const TsHardenCorpus& c) {
    const uint32_t app = find_fid(c.idx, "app.ts");
    const uint32_t foo = find_fid(c.idx, "foo.ts");
    CHECK(app != ~uint32_t{0});
    CHECK(foo != ~uint32_t{0});

    std::size_t count = 0;
    for (uint32_t n : c.graph.adj[app])
        if (n == foo)
            ++count;
    CHECK_EQ(count, 1u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // ---- Tiny corpus --------------------------------------------------------
    std::printf("=== Tiny corpus ===\n");
    auto corpus = make_tiny_corpus();

    std::printf("  %u files, %u terms, %zu graph edges\n", corpus.idx.num_docs,
                corpus.idx.vocab.size(), [&]() -> std::size_t {
                    std::size_t e = 0;
                    for (auto const& adj : corpus.graph.adj)
                        e += adj.size();
                    return e;
                }());

    test_webhook_postings_exactly_one_file(corpus);
    test_webhook_query_handler_ranks_first(corpus);
    test_graph_expands_importer_into_results(corpus);
    test_graph_disabled_excludes_pure_graph_files(corpus);
    test_indexed_and_naive_agree_on_top_result(corpus);
    test_auth_term_single_file_and_ranks_first(corpus);

    fs::remove_all(corpus.dir);

    // ---- Tiny TS/JS corpus --------------------------------------------------
    std::printf("\n=== Tiny TS/JS corpus ===\n");
    auto ts_corpus = make_ts_corpus();

    std::size_t ts_edges = 0;
    for (auto const& adj : ts_corpus.graph.adj)
        ts_edges += adj.size();
    std::printf("  %u files, %u terms, %zu graph edges\n", ts_corpus.idx.num_docs,
                ts_corpus.idx.vocab.size(), ts_edges);

    test_ts_corpus_parses_all_files(ts_corpus);
    test_ts_corpus_expected_edges_exist(ts_corpus);
    test_ts_corpus_bare_import_not_resolved(ts_corpus);
    test_ts_corpus_index_file_resolution(ts_corpus);

    fs::remove_all(ts_corpus.dir);

    // ---- TS/JS hardening corpus ---------------------------------------------
    std::printf("\n=== TS/JS hardening corpus ===\n");
    auto ts_harden = make_ts_harden_corpus();

    std::size_t ts_harden_edges = 0;
    for (auto const& adj : ts_harden.graph.adj)
        ts_harden_edges += adj.size();
    std::printf("  %u files, %u terms, %zu graph edges\n", ts_harden.idx.num_docs,
                ts_harden.idx.vocab.size(), ts_harden_edges);

    test_ts_harden_unresolved_makes_no_edge(ts_harden);
    test_ts_harden_extension_precedence_ts_over_js(ts_harden);
    test_ts_harden_path_normalization_with_dotdot(ts_harden);
    test_ts_harden_dedup_across_specifiers(ts_harden);

    fs::remove_all(ts_harden.dir);

    // ---- Python package corpus ----------------------------------------------
    std::printf("\n=== Python package corpus ===\n");
    auto py_pkg = make_py_pkg_corpus();

    std::size_t py_pkg_edges = 0;
    for (auto const& adj : py_pkg.graph.adj)
        py_pkg_edges += adj.size();
    std::printf("  %u files, %u terms, %zu graph edges\n", py_pkg.idx.num_docs,
                py_pkg.idx.vocab.size(), py_pkg_edges);

    test_py_pkg_all_files_parsed(py_pkg);
    test_py_pkg_absolute_dotted_import(py_pkg);
    test_py_pkg_absolute_from_import(py_pkg);
    test_py_pkg_single_dot_relative(py_pkg);
    test_py_pkg_relative_cross_sibling(py_pkg);
    test_py_pkg_double_dot_from_init(py_pkg);
    test_py_pkg_no_cross_package_collision(py_pkg);
    test_py_pkg_total_edges(py_pkg);

    fs::remove_all(py_pkg.dir);

    // ---- FastAPI corpus -----------------------------------------------------
    std::printf("\n=== FastAPI corpus ===\n");
    const fs::path fastapi_path = find_fastapi();

    if (fastapi_path.empty()) {
        std::printf("  SKIP: FastAPI not found. "
                    "Set RS_FASTAPI_PATH or install with pip install fastapi.\n");
        ++g_skipped;
    } else {
        std::printf("  source: %s\n", fastapi_path.c_str());

        auto fa_files = rs::parse_directory(fastapi_path);
        auto fa_naive = snapshot(fa_files);
        auto fa_idx = rs::build_index(std::move(fa_files));
        auto fa_graph = rs::build_graph(fa_idx);

        std::printf("  %u files, %u terms\n", fa_idx.num_docs, fa_idx.vocab.size());

        test_fastapi_background_file_ranks_first(fa_idx, fa_graph);
        test_fastapi_indexed_faster_than_naive(fa_idx, fa_graph, fa_naive);
    }

    // ---- Summary ------------------------------------------------------------
    std::printf("\n");
    if (g_skipped > 0)
        std::printf("Skipped: %d optional test(s).\n", g_skipped);

    const int total_run = 6 + 4 + 4 + 8 + (fastapi_path.empty() ? 0 : 2);
    if (g_failures == 0) {
        std::printf("All %d test(s) passed.\n", total_run);
        return 0;
    }
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failures);
    return 1;
}
