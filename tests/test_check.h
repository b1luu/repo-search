#pragma once

// ---------------------------------------------------------------------------
// Minimal hand-rolled test check macros.
//
// Design: no external framework, no new dependencies.  A CHECK failure prints
// the file/line/expression and increments a global counter; main() returns
// non-zero if any checks failed so CTest registers the job as failed.
//
// One TU per test binary, so `inline` gives each binary exactly one counter.
// ---------------------------------------------------------------------------

#include <cstdio>

inline int g_failures = 0;

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

#define CHECK_MSG(expr, fmt, ...)                                                                  \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL  %s:%d  " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
// NOLINTEND(cppcoreguidelines-macro-usage)
