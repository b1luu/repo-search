// Relevance regression tests.
//
// Two self-contained fixture corpora (one Python, one TS) exercise the full
// parse → index → graph → search pipeline against a declarative list of
// query-expectation cases. These are intentionally designed as a quality
// guardrail: a ranking change that meaningfully degrades relevance on these
// fixtures will fail here.
//
// Assertions avoid brittle float-score comparisons. They check:
//   - top-1 path match for lexically-unambiguous queries
//   - presence/absence in top-k for graph-assisted queries
//   - no-results for unknown terms
//   - descending score ordering
//   - deterministic results across repeated runs

#include "graph_builder.h"
#include "indexer.h"
#include "parser.h"
#include "search_engine.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_cases = 0;

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_MSG(expr, fmt, ...)                                                                  \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)

// ---------------------------------------------------------------------------
// Python fixture
//
// Each file carries a *signature token* that appears ONLY in that file
// (5 times) so lexical-only top-1 is unambiguous. Plus regular prose.
//
// Import edges (Python module-stem resolution):
//   orders.py     → payments, notifications
//   payments.py   → gateway
//   discounts.py  → payments
// Leaves: gateway.py, notifications.py, utils.py
// ---------------------------------------------------------------------------

static constexpr const char* PY_ORDERS = R"py(
"""Customer order lifecycle service."""
import payments
import notifications

# signature token: orderflow (appears only in this file)
# orderflow orderflow orderflow orderflow orderflow

class OrderService:
    """Place and fulfil customer orders end-to-end."""
    def place(self, customer, cart):
        record = self.build(customer, cart)
        payments.submit(record)
        notifications.deliver(record)
        return record

    def build(self, customer, cart):
        return {"customer": customer, "cart": cart}
)py";

static constexpr const char* PY_PAYMENTS = R"py(
"""Payment capture, refund, and settlement logic."""
import gateway

# signature token: refundable (appears only in this file)
# refundable refundable refundable refundable refundable

class PaymentProcessor:
    """Submit a charge through the upstream gateway."""
    def submit(self, record):
        return gateway.call("charge", record)

    def reverse(self, record):
        return gateway.call("reverse", record)
)py";

static constexpr const char* PY_GATEWAY = R"py(
"""Upstream payment gateway adapter."""

# signature token: stripeapi (appears only in this file)
# stripeapi stripeapi stripeapi stripeapi stripeapi

def call(action, record):
    """Issue an HTTPS call to the gateway."""
    return {"action": action, "record": record}
)py";

static constexpr const char* PY_NOTIFICATIONS = R"py(
"""Outbound notification dispatch."""

# signature token: pushnotice (appears only in this file)
# pushnotice pushnotice pushnotice pushnotice pushnotice

def deliver(record):
    """Push an outbound notification to the user."""
    return {"to": record.get("customer"), "kind": "notice"}
)py";

static constexpr const char* PY_DISCOUNTS = R"py(
"""Promotional discount rules."""
import payments

# signature token: couponly (appears only in this file)
# couponly couponly couponly couponly couponly

class DiscountRules:
    """Apply promotional rules before payment submission."""
    def apply(self, record, code):
        record["code"] = code
        return payments.submit(record)
)py";

static constexpr const char* PY_UTILS = R"py(
"""Generic helpers for serialization and IO."""

# signature token: serializer (appears only in this file)
# serializer serializer serializer serializer serializer

def to_json(obj):
    return str(obj)

def from_json(raw):
    return raw
)py";

// ---------------------------------------------------------------------------
// TS fixture
//
// Layout:
//   src/App.tsx                imports "./routes/Login", "./routes/Dashboard"
//   src/routes/Login.tsx       imports "../api/auth"
//   src/routes/Dashboard.tsx   imports "../api/client", "../lib/format"
//   src/api/auth.ts            (leaf)
//   src/api/client.ts          (leaf)
//   src/lib/format.ts          (leaf)
// ---------------------------------------------------------------------------

static constexpr const char* TS_APP = R"ts(
// signature: appshell appshell appshell appshell appshell
import { Login } from "./routes/Login";
import { Dashboard } from "./routes/Dashboard";

export function App() {
  return { Login, Dashboard };
}
)ts";

static constexpr const char* TS_LOGIN = R"ts(
// signature: loginform loginform loginform loginform loginform
import { signIn } from "../api/auth";

export function Login(username: string, password: string) {
  return signIn(username, password);
}
)ts";

static constexpr const char* TS_DASHBOARD = R"ts(
// signature: dashview dashview dashview dashview dashview
import { fetchJson } from "../api/client";
import { formatMoney } from "../lib/format";

export function Dashboard() {
  return formatMoney(fetchJson("/widgets"));
}
)ts";

static constexpr const char* TS_AUTH = R"ts(
// signature: authtoken authtoken authtoken authtoken authtoken
export function signIn(user: string, pass: string) {
  return { user, pass };
}
)ts";

static constexpr const char* TS_CLIENT = R"ts(
// signature: httpfetch httpfetch httpfetch httpfetch httpfetch
export function fetchJson(url: string) {
  return { url };
}
)ts";

static constexpr const char* TS_FORMAT = R"ts(
// signature: currencyfmt currencyfmt currencyfmt currencyfmt currencyfmt
export function formatMoney(x: unknown) {
  return String(x);
}
)ts";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void write_file(const fs::path& p, const char* content) {
    std::ofstream f(p);
    f << content;
}

struct Corpus {
    rs::Index idx;
    rs::Graph graph;
    fs::path dir;
};

static Corpus make_py_corpus() {
    auto dir = fs::temp_directory_path() / "rs_relevance_py";
    fs::remove_all(dir);
    fs::create_directories(dir);

    write_file(dir / "orders.py", PY_ORDERS);
    write_file(dir / "payments.py", PY_PAYMENTS);
    write_file(dir / "gateway.py", PY_GATEWAY);
    write_file(dir / "notifications.py", PY_NOTIFICATIONS);
    write_file(dir / "discounts.py", PY_DISCOUNTS);
    write_file(dir / "utils.py", PY_UTILS);

    auto files = rs::parse_directory(dir);
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph), dir};
}

static Corpus make_ts_corpus() {
    auto dir = fs::temp_directory_path() / "rs_relevance_ts";
    fs::remove_all(dir);
    fs::create_directories(dir / "src" / "routes");
    fs::create_directories(dir / "src" / "api");
    fs::create_directories(dir / "src" / "lib");

    write_file(dir / "src" / "App.tsx", TS_APP);
    write_file(dir / "src" / "routes" / "Login.tsx", TS_LOGIN);
    write_file(dir / "src" / "routes" / "Dashboard.tsx", TS_DASHBOARD);
    write_file(dir / "src" / "api" / "auth.ts", TS_AUTH);
    write_file(dir / "src" / "api" / "client.ts", TS_CLIENT);
    write_file(dir / "src" / "lib" / "format.ts", TS_FORMAT);

    auto files = rs::parse_directory(dir);
    auto idx = rs::build_index(std::move(files));
    auto graph = rs::build_graph(idx);
    return {std::move(idx), std::move(graph), dir};
}

static bool contains_suffix(const std::vector<rs::SearchResult>& rs, std::string_view suffix) {
    for (auto const& r : rs)
        if (r.path.ends_with(suffix))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Case runner
// ---------------------------------------------------------------------------

struct Case {
    const char* label;
    const char* query;
    float alpha;
    uint32_t top_k;
    const char* expect_top1;                    // nullptr → not asserted
    std::initializer_list<const char*> in_topk; // suffixes that MUST be present
    std::initializer_list<const char*> not_in;  // suffixes that MUST be absent
    bool expect_empty;
};

static void run_case(const Corpus& c, const Case& tc) {
    ++g_cases;

    rs::SearchParams params;
    params.top_k = tc.top_k;
    params.graph_alpha = tc.alpha;

    auto results = rs::search(tc.query, c.idx, c.graph, params);

    if (tc.expect_empty) {
        CHECK_MSG(results.empty(), "[%s] expected empty, got %zu", tc.label, results.size());
        return;
    }

    CHECK_MSG(!results.empty(), "[%s] expected results, got empty", tc.label);
    if (results.empty())
        return;

    // Descending by score.
    for (std::size_t i = 1; i < results.size(); ++i) {
        CHECK_MSG(results[i - 1].score >= results[i].score,
                  "[%s] scores not descending at index %zu", tc.label, i);
    }

    // All scores strictly positive (a zero-score entry would indicate the
    // ranker leaked irrelevant candidates into the output).
    for (auto const& r : results) {
        CHECK_MSG(r.score > 0.0f, "[%s] non-positive score for %.*s", tc.label,
                  static_cast<int>(r.path.size()), r.path.data());
    }

    if (tc.expect_top1 != nullptr) {
        const bool ok = results[0].path.ends_with(tc.expect_top1);
        CHECK_MSG(ok, "[%s] top1=%.*s, expected suffix %s", tc.label,
                  static_cast<int>(results[0].path.size()), results[0].path.data(), tc.expect_top1);
    }

    for (const char* suffix : tc.in_topk) {
        CHECK_MSG(contains_suffix(results, suffix), "[%s] missing from top-%u: %s", tc.label,
                  tc.top_k, suffix);
    }
    for (const char* suffix : tc.not_in) {
        CHECK_MSG(!contains_suffix(results, suffix), "[%s] must not appear in top-%u: %s", tc.label,
                  tc.top_k, suffix);
    }
}

// ---------------------------------------------------------------------------
// Python cases
// ---------------------------------------------------------------------------

static void run_python_cases(const Corpus& c) {
    // --- Lexical-only (alpha = 0): unambiguous signature tokens. ------------
    run_case(c, {"py/refund", "refundable", 0.0f, 5, "payments.py", {}, {}, false});
    run_case(c, {"py/orderflow", "orderflow", 0.0f, 5, "orders.py", {}, {}, false});
    run_case(c, {"py/stripeapi", "stripeapi", 0.0f, 5, "gateway.py", {}, {}, false});
    run_case(c, {"py/pushnotice", "pushnotice", 0.0f, 5, "notifications.py", {}, {}, false});
    run_case(c, {"py/couponly", "couponly", 0.0f, 5, "discounts.py", {}, {}, false});
    run_case(c, {"py/serializer", "serializer", 0.0f, 5, "utils.py", {}, {}, false});

    // --- Graph expansion (alpha > 0): importers of the match file must
    //     appear via 1-hop graph bonus even though they carry zero lexical
    //     score for the query. ------------------------------------------------
    // payments imports gateway → query "stripeapi" should surface payments.py.
    run_case(c, {"py/graph/stripeapi→payments",
                 "stripeapi",
                 0.20f,
                 10,
                 "gateway.py",
                 {"payments.py"},
                 {},
                 false});
    // orders and discounts both import payments → query "refundable" should
    // surface both as pure graph neighbors.
    run_case(c, {"py/graph/refund→orders+discounts",
                 "refundable",
                 0.20f,
                 10,
                 "payments.py",
                 {"orders.py", "discounts.py"},
                 {},
                 false});

    // --- Lexical-only must NOT leak pure graph neighbors. -------------------
    run_case(c, {"py/lex-only/refund excludes neighbors",
                 "refundable",
                 0.0f,
                 10,
                 "payments.py",
                 {},
                 {"orders.py", "discounts.py"},
                 false});

    // --- Unknown term → no results. -----------------------------------------
    run_case(c, {"py/unknown", "zzzneverappearszzz", 0.15f, 10, nullptr, {}, {}, true});
}

// ---------------------------------------------------------------------------
// TS cases
// ---------------------------------------------------------------------------

static void run_ts_cases(const Corpus& c) {
    // --- Lexical-only (alpha = 0): unambiguous signature tokens. ------------
    run_case(c, {"ts/loginform", "loginform", 0.0f, 5, "Login.tsx", {}, {}, false});
    run_case(c, {"ts/dashview", "dashview", 0.0f, 5, "Dashboard.tsx", {}, {}, false});
    run_case(c, {"ts/authtoken", "authtoken", 0.0f, 5, "api/auth.ts", {}, {}, false});
    run_case(c, {"ts/httpfetch", "httpfetch", 0.0f, 5, "api/client.ts", {}, {}, false});
    run_case(c, {"ts/currencyfmt", "currencyfmt", 0.0f, 5, "lib/format.ts", {}, {}, false});
    run_case(c, {"ts/appshell", "appshell", 0.0f, 5, "App.tsx", {}, {}, false});

    // --- Graph expansion: importers of the match file appear in top-k. -----
    // Login.tsx imports ../api/auth → "authtoken" query should surface Login.
    run_case(c, {"ts/graph/authtoken→Login",
                 "authtoken",
                 0.20f,
                 10,
                 "api/auth.ts",
                 {"Login.tsx"},
                 {},
                 false});
    // Dashboard.tsx imports ../lib/format → "currencyfmt" query should surface
    // Dashboard.tsx.
    run_case(c, {"ts/graph/currencyfmt→Dashboard",
                 "currencyfmt",
                 0.20f,
                 10,
                 "lib/format.ts",
                 {"Dashboard.tsx"},
                 {},
                 false});

    // --- Lexical-only must NOT leak pure graph neighbors. -------------------
    run_case(c, {"ts/lex-only/authtoken excludes Login",
                 "authtoken",
                 0.0f,
                 10,
                 "api/auth.ts",
                 {},
                 {"Login.tsx"},
                 false});

    // --- Unknown term → no results. -----------------------------------------
    run_case(c, {"ts/unknown", "qqqneverappearsqqq", 0.15f, 10, nullptr, {}, {}, true});
}

// ---------------------------------------------------------------------------
// Determinism
//
// Two identical searches on an unchanged Index must return byte-identical
// result sequences. Any nondeterminism (unstable sort, hash iteration order,
// etc.) would be caught here.
// ---------------------------------------------------------------------------

static void test_determinism(const Corpus& c) {
    ++g_cases;
    rs::SearchParams p;
    p.top_k = 10;
    p.graph_alpha = 0.15f;

    auto a = rs::search("refundable", c.idx, c.graph, p);
    auto b = rs::search("refundable", c.idx, c.graph, p);

    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK(a[i].file_id == b[i].file_id);
        CHECK(a[i].path == b[i].path);
        CHECK(a[i].score == b[i].score);
    }
}

// ---------------------------------------------------------------------------

int main() {
    std::printf("=== Relevance regression suite ===\n");

    auto py = make_py_corpus();
    std::printf("  python fixture: %u files, %u terms\n", py.idx.num_docs, py.idx.vocab.size());
    run_python_cases(py);
    test_determinism(py);
    fs::remove_all(py.dir);

    auto ts = make_ts_corpus();
    std::printf("  ts fixture:     %u files, %u terms\n", ts.idx.num_docs, ts.idx.vocab.size());
    run_ts_cases(ts);
    fs::remove_all(ts.dir);

    if (g_failures == 0) {
        std::printf("All %d relevance case(s) passed.\n", g_cases);
        return 0;
    }
    std::fprintf(stderr, "%d failure(s) across %d case(s).\n", g_failures, g_cases);
    return 1;
}
