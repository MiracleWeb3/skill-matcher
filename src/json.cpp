#include "json.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sm::json {
namespace {

struct Parser {
    std::string_view s;
    std::size_t i = 0;
    bool bad = false;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
    void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    std::string string_() {
        std::string out;
        if (!eat('"')) { bad = true; return out; }
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (i >= s.size()) break;
            switch (const char e = s[i++]) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case '"': case '\\': case '/': out.push_back(e); break;
                case 'u': {
                    if (i + 4 > s.size()) { bad = true; return out; }
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else { bad = true; return out; }
                    }
                    i += 4;
                    utf8(out, cp);
                    break;
                }
                default: bad = true; return out;
            }
        }
        bad = true;
        return out;
    }
    Ptr value() {
        ws();
        if (bad || i >= s.size()) { bad = true; return nullptr; }
        auto v = std::make_shared<Value>();
        const char c = s[i];
        if (c == '{') {
            ++i;
            v->kind = Value::Kind::Object;
            ws();
            if (eat('}')) return v;
            for (;;) {
                ws();
                std::string k = string_();
                if (bad || !eat(':')) { bad = true; return nullptr; }
                auto child = value();
                if (bad) return nullptr;
                v->object.emplace_back(std::move(k), std::move(child));
                if (eat(',')) continue;
                if (eat('}')) return v;
                bad = true;
                return nullptr;
            }
        }
        if (c == '[') {
            ++i;
            v->kind = Value::Kind::Array;
            ws();
            if (eat(']')) return v;
            for (;;) {
                auto child = value();
                if (bad) return nullptr;
                v->array.push_back(std::move(child));
                if (eat(',')) continue;
                if (eat(']')) return v;
                bad = true;
                return nullptr;
            }
        }
        if (c == '"') { v->kind = Value::Kind::String; v->str = string_(); return bad ? nullptr : v; }
        if (s.compare(i, 4, "true") == 0) { i += 4; v->kind = Value::Kind::Bool; v->boolean = true; return v; }
        if (s.compare(i, 5, "false") == 0) { i += 5; v->kind = Value::Kind::Bool; return v; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return v; }
        const std::size_t start = i;
        while (i < s.size() && (std::string_view("+-.eE0123456789").find(s[i]) != std::string_view::npos)) ++i;
        if (i == start) { bad = true; return nullptr; }
        v->kind = Value::Kind::Number;
        v->number = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
        return v;
    }
};

}  // namespace

const Value* Value::get(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& [k, v] : object) {
        if (k == key) return v.get();
    }
    return nullptr;
}

Ptr parse(std::string_view text) {
    Parser p{text};
    auto v = p.value();
    return p.bad ? nullptr : v;
}

Ptr parse_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();
    return parse(text);
}

}  // namespace sm::json
