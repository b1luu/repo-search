#include "js_imports.h"

namespace rs {

void extract_js_imports(std::string_view content, std::vector<std::string>& out) {
    enum class State {
        None,
        ImportHead,
        ImportClause,
        ExportHead,
        SawRequire,
        ExpectString,
    };
    State state = State::None;
    bool prev_was_dot = false;

    auto is_ident_start = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    };
    auto is_ident_char = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '$';
    };

    const char* p = content.data();
    const char* const end = p + content.size();

    while (p < end) {
        const unsigned char c = static_cast<unsigned char>(*p);

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++p;
            continue;
        }

        if (c == '/' && p + 1 < end && p[1] == '/') {
            p += 2;
            while (p < end && *p != '\n')
                ++p;
            continue;
        }

        if (c == '/' && p + 1 < end && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                ++p;
            if (p + 1 < end)
                p += 2;
            else
                p = end;
            continue;
        }

        if (c == '"' || c == '\'' || c == '`') {
            const char quote = static_cast<char>(c);
            ++p;
            const char* const str_start = p;
            while (p < end && *p != quote) {
                if (*p == '\\' && p + 1 < end)
                    p += 2;
                else
                    ++p;
            }
            const char* const str_end = p;
            if (p < end)
                ++p;

            if ((state == State::ImportHead || state == State::ExpectString) && quote != '`') {
                out.emplace_back(str_start, static_cast<std::size_t>(str_end - str_start));
            }
            state = State::None;
            prev_was_dot = false;
            continue;
        }

        if (c == '(') {
            if (state == State::ImportHead || state == State::SawRequire)
                state = State::ExpectString;
            else
                state = State::None;
            ++p;
            prev_was_dot = false;
            continue;
        }

        if (c == '.') {
            state = State::None;
            prev_was_dot = true;
            ++p;
            continue;
        }

        if (c == '=' || c == ';') {
            state = State::None;
            prev_was_dot = false;
            ++p;
            continue;
        }

        if (is_ident_start(c)) {
            const char* const word_start = p;
            while (p < end && is_ident_char(static_cast<unsigned char>(*p)))
                ++p;
            const std::string_view w(word_start, static_cast<std::size_t>(p - word_start));
            const bool after_dot = prev_was_dot;
            prev_was_dot = false;

            if (state == State::ExpectString) {
                state = State::None;
                continue;
            }

            if (after_dot)
                continue;

            if (w == "import") {
                state = State::ImportHead;
            } else if (w == "export") {
                state = State::ExportHead;
            } else if (w == "from") {
                if (state == State::ImportHead || state == State::ImportClause ||
                    state == State::ExportHead) {
                    state = State::ExpectString;
                } else {
                    state = State::None;
                }
            } else if (w == "require") {
                state = State::SawRequire;
            } else {
                if (state == State::ImportHead)
                    state = State::ImportClause;
                else if (state == State::SawRequire)
                    state = State::None;
            }
            continue;
        }

        if (state == State::ExpectString || state == State::SawRequire)
            state = State::None;
        else if (state == State::ImportHead)
            state = State::ImportClause;
        prev_was_dot = false;
        ++p;
    }
}

} // namespace rs
