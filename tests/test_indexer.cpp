#include "indexer.h"
#include "parser.h"
#include "tokenizer.h"

#include <cstdio>
#include <string>
#include <vector>

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
// Helpers
// ---------------------------------------------------------------------------

// Construct a synthetic ParsedFile.  Content strings must be long enough
// to be heap-allocated (> ~22 chars on libc++) so that moving the string
// transfers the heap pointer and keeps token_views valid.  See indexer.h.
static rs::ParsedFile make_pf(std::string path, std::string content,
                              std::vector<std::string> imports = {}) {
    rs::ParsedFile pf;
    pf.path = std::move(path);
    pf.content = std::move(content);
    pf.imports = std::move(imports);
    // tokenize() lowercases content in-place and returns views into it.
    pf.token_views = rs::tokenize(pf.content);
    return pf;
}

// Build a small index from a set of synthetic files.
// reserve() prevents vector reallocation, which would invalidate token_views
// for any content string stored in SSO (< ~22 chars).
static std::vector<rs::ParsedFile> make_files(
    std::initializer_list<std::tuple<std::string, std::string, std::vector<std::string>>> specs) {
    std::vector<rs::ParsedFile> files;
    files.reserve(specs.size());
    for (auto& [path, content, imports] : specs) {
        files.push_back(make_pf(path, content, imports));
    }
    return files;
}

// ---------------------------------------------------------------------------
// Vocabulary tests
// ---------------------------------------------------------------------------

static void test_vocab_intern_same_id() {
    rs::Vocabulary v;
    const uint32_t id1 = v.intern("hello");
    const uint32_t id2 = v.intern("hello");
    CHECK_EQ(id1, id2);
}

static void test_vocab_intern_sequential_ids() {
    rs::Vocabulary v;
    const uint32_t a = v.intern("alpha");
    const uint32_t b = v.intern("beta");
    const uint32_t c = v.intern("gamma");
    // IDs must be distinct and sequential from 0.
    CHECK_EQ(a, 0u);
    CHECK_EQ(b, 1u);
    CHECK_EQ(c, 2u);
    CHECK_EQ(v.size(), 3u);
}

static void test_vocab_find_known() {
    rs::Vocabulary v;
    v.intern("hello");
    const uint32_t id = v.find("hello");
    CHECK(id != ~uint32_t{0});
    CHECK_EQ(id, 0u);
}

static void test_vocab_find_unknown() {
    rs::Vocabulary v;
    v.intern("hello");
    CHECK_EQ(v.find("world"), ~uint32_t{0});
}

static void test_vocab_id_to_term_roundtrip() {
    rs::Vocabulary v;
    v.intern("foo");
    v.intern("bar");
    CHECK(v.id_to_term[0] == "foo");
    CHECK(v.id_to_term[1] == "bar");
}

// ---------------------------------------------------------------------------
// build_index tests
// ---------------------------------------------------------------------------

static void test_index_doc_count() {
    auto files = make_files({
        {"a.py", "define search query result computation storage", {}},
        {"b.py", "class result data buffer allocation management", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));
    CHECK_EQ(idx.num_docs, 2u);
    CHECK_EQ(idx.paths.size(), 2u);
}

static void test_index_terms_present_in_vocab() {
    auto files = make_files({
        {"a.py", "define search query result computation storage", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));
    // "result" appears in the file — must be in the vocab.
    CHECK(idx.vocab.find("result") != ~uint32_t{0});
    // "xyz" does not appear — must be absent.
    CHECK_EQ(idx.vocab.find("xyz"), ~uint32_t{0});
}

static void test_index_postings_correct_file() {
    // "unique" appears only in file 0; "common" appears in both.
    auto files = make_files({
        {"a.py", "unique term appears only here in file alpha search", {}},
        {"b.py", "another file different content none matched storage system", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));

    const uint32_t tid_unique = idx.vocab.find("unique");
    CHECK(tid_unique != ~uint32_t{0});
    // "unique" should appear only in file 0.
    CHECK_EQ(idx.postings[tid_unique].size(), 1u);
    CHECK_EQ(idx.postings[tid_unique][0].file_id, 0u);
}

static void test_index_postings_both_files() {
    auto files = make_files({
        {"a.py", "common search term appears in both files alphabetical", {}},
        {"b.py", "common term appears here too in second file storage", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));

    const uint32_t tid_common = idx.vocab.find("common");
    CHECK(tid_common != ~uint32_t{0});
    CHECK_EQ(idx.postings[tid_common].size(), 2u);
    // Postings must be sorted by file_id (ascending).
    CHECK_EQ(idx.postings[tid_common][0].file_id, 0u);
    CHECK_EQ(idx.postings[tid_common][1].file_id, 1u);
}

static void test_index_tf_normalized() {
    // File with 10 tokens, one occurrence of "search" → tf = 1/10 = 0.1
    auto files = make_files({
        {"a.py", "search alpha beta gamma delta epsilon zeta eta theta iota", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));
    CHECK_EQ(idx.doc_lengths[0], 10u);

    const uint32_t tid = idx.vocab.find("search");
    CHECK(tid != ~uint32_t{0});
    CHECK_EQ(idx.postings[tid].size(), 1u);
    // TF should be approximately 0.1 (within floating-point rounding).
    const float tf = idx.postings[tid][0].tf;
    CHECK(tf > 0.09f && tf < 0.11f);
}

static void test_index_tf_higher_for_repeated_term() {
    // "search" appears 3 times out of 10 tokens → tf ≈ 0.3
    auto files = make_files({
        {"a.py", "search search search alpha beta gamma delta epsilon zeta eta", {}},
    });
    const rs::Index idx = rs::build_index(std::move(files));

    const uint32_t tid = idx.vocab.find("search");
    CHECK(tid != ~uint32_t{0});
    const float tf = idx.postings[tid][0].tf;
    CHECK(tf > 0.29f && tf < 0.31f);
}

static void test_index_empty_file() {
    // A file with no tokens should produce an entry with doc_length == 0.
    auto files = make_files({
        {"empty.py", "!@#$%^", {}}, // only delimiters → no tokens
    });
    const rs::Index idx = rs::build_index(std::move(files));
    CHECK_EQ(idx.num_docs, 1u);
    CHECK_EQ(idx.doc_lengths[0], 0u);
}

static void test_index_imports_preserved() {
    auto files = make_files({
        {"a.py", "import statement file content parsing analysis", {"os", "sys"}},
    });
    const rs::Index idx = rs::build_index(std::move(files));
    CHECK_EQ(idx.file_imports[0].size(), 2u);
    CHECK(idx.file_imports[0][0] == "os");
    CHECK(idx.file_imports[0][1] == "sys");
}

// ---------------------------------------------------------------------------

int main() {
    // Vocabulary
    test_vocab_intern_same_id();
    test_vocab_intern_sequential_ids();
    test_vocab_find_known();
    test_vocab_find_unknown();
    test_vocab_id_to_term_roundtrip();

    // build_index
    test_index_doc_count();
    test_index_terms_present_in_vocab();
    test_index_postings_correct_file();
    test_index_postings_both_files();
    test_index_tf_normalized();
    test_index_tf_higher_for_repeated_term();
    test_index_empty_file();
    test_index_imports_preserved();

    const int total = 13;
    if (g_failures == 0) {
        std::printf("All %d tests passed.\n", total);
        return 0;
    }
    std::fprintf(stderr, "%d/%d test(s) FAILED.\n", g_failures, total);
    return 1;
}
