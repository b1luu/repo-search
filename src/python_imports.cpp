#include "python_imports.h"

#include <cctype>

namespace rs {

void extract_python_imports(std::string_view content, std::vector<std::string>& out) {
    const char* p = content.data();
    const char* end = p + content.size();

    auto skip_spaces = [](const char* s, const char* e) {
        while (s < e && (*s == ' ' || *s == '\t'))
            ++s;
        return s;
    };

    auto read_identifier = [](const char* s, const char* e, std::string& out_id) {
        out_id.clear();
        while (s < e && (std::isalnum(static_cast<unsigned char>(*s)) || *s == '_' || *s == '.')) {
            out_id += *s++;
        }
        return s;
    };

    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n')
            ++line_end;

        const char* lp = skip_spaces(p, line_end);

        if (line_end - lp >= 7 && lp[0] == 'i' && lp[1] == 'm' && lp[2] == 'p' && lp[3] == 'o' &&
            lp[4] == 'r' && lp[5] == 't' && (lp[6] == ' ' || lp[6] == '\t')) {

            lp += 7;
            while (lp < line_end) {
                lp = skip_spaces(lp, line_end);
                std::string mod;
                lp = read_identifier(lp, line_end, mod);
                if (!mod.empty())
                    out.push_back(mod);

                lp = skip_spaces(lp, line_end);
                if (line_end - lp >= 3 && lp[0] == 'a' && lp[1] == 's' &&
                    (lp[2] == ' ' || lp[2] == '\t')) {
                    lp += 3;
                    std::string alias;
                    lp = read_identifier(lp, line_end, alias);
                }
                lp = skip_spaces(lp, line_end);
                if (lp < line_end && *lp == ',')
                    ++lp;
                else
                    break;
            }

        } else if (line_end - lp >= 5 && lp[0] == 'f' && lp[1] == 'r' && lp[2] == 'o' &&
                   lp[3] == 'm' && (lp[4] == ' ' || lp[4] == '\t')) {

            lp += 5;
            lp = skip_spaces(lp, line_end);
            std::string mod;
            lp = read_identifier(lp, line_end, mod);
            if (!mod.empty())
                out.push_back(mod);
        }

        p = line_end + 1;
    }
}

} // namespace rs
