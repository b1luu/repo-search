#include "cli.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace rs {

static bool parse_uint(std::string_view s, uint32_t& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

static bool parse_float(std::string_view s, float& out) {
    // std::from_chars<float> isn't universally available on older libc++; use strtof.
    std::string buf(s);
    char* end = nullptr;
    out = std::strtof(buf.c_str(), &end);
    return end != buf.c_str() && *end == '\0';
}

void print_usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [--top-k N] [--alpha F] <directory> <query>\n"
                 "  --top-k N   number of results to return (default 10)\n"
                 "  --alpha F   graph expansion weight in [0,1] (default 0.15)\n",
                 prog);
}

CliParseResult parse_cli(int argc, char* argv[], CliOptions& out) {
    const char* dir_arg = nullptr;
    const char* query_arg = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        auto take_value = [&](std::string_view flag, std::string_view& val) -> bool {
            if (a == flag) {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "Error: %.*s requires a value.\n",
                                 static_cast<int>(flag.size()), flag.data());
                    return false;
                }
                val = argv[++i];
                return true;
            }
            if (a.size() > flag.size() + 1 && a.substr(0, flag.size()) == flag &&
                a[flag.size()] == '=') {
                val = a.substr(flag.size() + 1);
                return true;
            }
            return false;
        };

        std::string_view val;
        if (a == "--top-k" || a.substr(0, 8) == "--top-k=") {
            if (!take_value("--top-k", val))
                return CliParseResult::Error;
            if (!parse_uint(val, out.params.top_k) || out.params.top_k == 0) {
                std::fprintf(stderr, "Error: --top-k expects a positive integer.\n");
                return CliParseResult::Error;
            }
        } else if (a == "--alpha" || a.substr(0, 8) == "--alpha=") {
            if (!take_value("--alpha", val))
                return CliParseResult::Error;
            if (!parse_float(val, out.params.graph_alpha) || out.params.graph_alpha < 0.0f ||
                out.params.graph_alpha > 1.0f) {
                std::fprintf(stderr, "Error: --alpha expects a float in [0,1].\n");
                return CliParseResult::Error;
            }
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return CliParseResult::Help;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "Error: unknown flag '%.*s'.\n", static_cast<int>(a.size()),
                         a.data());
            print_usage(argv[0]);
            return CliParseResult::Error;
        } else if (!dir_arg) {
            dir_arg = argv[i];
        } else if (!query_arg) {
            query_arg = argv[i];
        } else {
            std::fprintf(stderr, "Error: unexpected positional '%s'.\n", argv[i]);
            return CliParseResult::Error;
        }
    }

    if (!dir_arg || !query_arg) {
        print_usage(argv[0]);
        return CliParseResult::Error;
    }

    out.root = std::filesystem::path(dir_arg);
    out.query = query_arg;
    return CliParseResult::Ok;
}

} // namespace rs
