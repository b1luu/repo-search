#include "tokenizer.h"

#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal hand-rolled test runner.
//
// Design: no external framework, no new dependencies.  A CHECK failure prints
// the file/line/expression and increments a global counter; main() returns
// non-zero if any checks failed so CTest registers the job as failed.
// ---------------------------------------------------------------------------

static int g_failures = 0;

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                  \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        if ((a) != (b)) {                                                                          \
            std::fprintf(stderr, "FAIL  %s:%d  (%s) == (%s)\n", __FILE__, __LINE__, #a, #b);       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)

// ---------------------------------------------------------------------------
// tokenize() tests
// ---------------------------------------------------------------------------

static void test_basic_split() {
    std::string text = "Hello, World!";
    auto toks = rs::tokenize(text);
    CHECK_EQ(toks.size(), 2u);
    CHECK(toks[0] == "hello");
    CHECK(toks[1] == "world");
}

static void test_lowercases_in_place() {
    std::string text = "FOO BAR";
    rs::tokenize(text);
    // tokenize() mutates the buffer to lowercase
    CHECK(text == "foo bar");
}

static void test_skips_single_char_tokens() {
    std::string text = "a bb ccc";
    auto toks = rs::tokenize(text);
    // 'a' is length 1 and must be dropped; 'bb' and 'ccc' are kept
    CHECK_EQ(toks.size(), 2u);
    CHECK(toks[0] == "bb");
    CHECK(toks[1] == "ccc");
}

static void test_empty_string() {
    std::string text;
    auto toks = rs::tokenize(text);
    CHECK(toks.empty());
}

static void test_only_delimiters() {
    std::string text = "!@#$%^&*() \t\n";
    auto toks = rs::tokenize(text);
    CHECK(toks.empty());
}

static void test_views_point_into_buffer() {
    std::string text = "hello world";
    auto toks = rs::tokenize(text);
    CHECK_EQ(toks.size(), 2u);
    // Each view must be a subrange of the original buffer.
    const char* base = text.data();
    const char* end = base + text.size();
    for (auto const& tok : toks) {
        CHECK(tok.data() >= base);
        CHECK(tok.data() + tok.size() <= end);
    }
}

static void test_underscore_splits_tokens() {
    // Underscores are not alphanumeric — compute_bm25 → "compute" + "bm25"
    std::string text = "compute_bm25";
    auto toks = rs::tokenize(text);
    CHECK_EQ(toks.size(), 2u);
    CHECK(toks[0] == "compute");
    CHECK(toks[1] == "bm25");
}

static void test_python_function_signature() {
    std::string text = "def compute_bm25(query, docs):";
    auto toks = rs::tokenize(text);
    // Expected: def, compute, bm25, query, docs
    CHECK_EQ(toks.size(), 5u);
    CHECK(toks[0] == "def");
    CHECK(toks[1] == "compute");
    CHECK(toks[2] == "bm25");
    CHECK(toks[3] == "query");
    CHECK(toks[4] == "docs");
}

static void test_numbers_kept() {
    std::string text = "version 42 x86";
    auto toks = rs::tokenize(text);
    CHECK_EQ(toks.size(), 3u);
    CHECK(toks[0] == "version");
    CHECK(toks[1] == "42");
    CHECK(toks[2] == "x86");
}

static void test_reserve_does_not_over_allocate_on_small_input() {
    // Just a smoke test — tokenize should not crash on very short strings.
    std::string text = "ab";
    auto toks = rs::tokenize(text);
    CHECK_EQ(toks.size(), 1u);
    CHECK(toks[0] == "ab");
}

// ---------------------------------------------------------------------------
// tokenize_owned() tests
// ---------------------------------------------------------------------------

static void test_owned_basic() {
    auto toks = rs::tokenize_owned("Hello World");
    CHECK_EQ(toks.size(), 2u);
    CHECK(toks[0] == "hello");
    CHECK(toks[1] == "world");
}

static void test_owned_empty() {
    auto toks = rs::tokenize_owned("");
    CHECK(toks.empty());
}

static void test_owned_does_not_mutate_input() {
    // tokenize_owned takes string_view — it should not affect the original.
    const std::string original = "HELLO WORLD";
    auto toks = rs::tokenize_owned(original);
    CHECK(original == "HELLO WORLD"); // unchanged
    CHECK_EQ(toks.size(), 2u);
    CHECK(toks[0] == "hello");
}

// ---------------------------------------------------------------------------

int main() {
    test_basic_split();
    test_lowercases_in_place();
    test_skips_single_char_tokens();
    test_empty_string();
    test_only_delimiters();
    test_views_point_into_buffer();
    test_underscore_splits_tokens();
    test_python_function_signature();
    test_numbers_kept();
    test_reserve_does_not_over_allocate_on_small_input();
    test_owned_basic();
    test_owned_empty();
    test_owned_does_not_mutate_input();

    const int total = 13;
    if (g_failures == 0) {
        std::printf("All %d tests passed.\n", total);
        return 0;
    }
    std::fprintf(stderr, "%d/%d test(s) FAILED.\n", g_failures, total);
    return 1;
}
