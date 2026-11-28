#pragma once

#include "num_parse.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace seb::json {

struct JsonVal;
using JsonArr = std::vector<JsonVal>;
using JsonObj = std::vector<std::pair<std::string, JsonVal>>;

struct JsonVal {
    std::variant<std::nullptr_t, bool, int64_t, uint64_t, double, std::string, JsonArr, JsonObj> data;

    JsonVal() : data(nullptr) {}
    JsonVal(std::nullptr_t) : data(nullptr) {}
    JsonVal(bool b) : data(b) {}
    JsonVal(int64_t i) : data(i) {}
    JsonVal(uint64_t u) : data(u) {}
    JsonVal(int i) : data(static_cast<int64_t>(i)) {}
    JsonVal(double d) : data(d) {}
    JsonVal(std::string s) : data(std::move(s)) {}
    JsonVal(const char* s) : data(std::string(s)) {}
    JsonVal(JsonArr a) : data(std::move(a)) {}
    JsonVal(JsonObj o) : data(std::move(o)) {}

    [[nodiscard]] const JsonVal* get(std::string_view key) const {
        if (auto* o = std::get_if<JsonObj>(&data)) {
            for (const auto& [k, v] : *o) {
                if (k == key) return &v;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::string* as_str() const { return std::get_if<std::string>(&data); }
    [[nodiscard]] const bool* as_bool() const { return std::get_if<bool>(&data); }
    [[nodiscard]] const int64_t* as_i64() const { return std::get_if<int64_t>(&data); }
    [[nodiscard]] const uint64_t* as_u64() const { return std::get_if<uint64_t>(&data); }
    [[nodiscard]] const double* as_dbl() const { return std::get_if<double>(&data); }
    [[nodiscard]] const JsonArr* as_arr() const { return std::get_if<JsonArr>(&data); }
    [[nodiscard]] const JsonObj* as_obj() const { return std::get_if<JsonObj>(&data); }

    bool get_i64(int64_t& out) const {
        if (auto* i = std::get_if<int64_t>(&data)) { out = *i; return true; }
        if (auto* u = std::get_if<uint64_t>(&data)) {
            if (*u <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                out = static_cast<int64_t>(*u);
                return true;
            }
            return false;
        }
        if (auto* d = std::get_if<double>(&data)) {
            if (std::isfinite(*d) && *d >= -9223372036854775808.0 && *d < 9223372036854775808.0) {
                out = static_cast<int64_t>(*d);
                return true;
            }
            return false;
        }
        return false;
    }

    bool get_u64(uint64_t& out) const {
        if (auto* u = std::get_if<uint64_t>(&data)) { out = *u; return true; }
        if (auto* i = std::get_if<int64_t>(&data)) {
            if (*i >= 0) {
                out = static_cast<uint64_t>(*i);
                return true;
            }
            return false;
        }
        if (auto* d = std::get_if<double>(&data)) {
            if (std::isfinite(*d) && *d >= 0.0 && *d < 18446744073709551616.0) {
                out = static_cast<uint64_t>(*d);
                return true;
            }
            return false;
        }
        return false;
    }
};

inline void append_escaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char H[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(H[(c >> 4) & 0xFu]);
                    out.push_back(H[c & 0xFu]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

inline void stringify_into(const JsonVal& v, std::string& out) {
    if (std::holds_alternative<std::nullptr_t>(v.data)) {
        out += "null";
    } else if (auto* b = std::get_if<bool>(&v.data)) {
        out += (*b ? "true" : "false");
    } else if (auto* i = std::get_if<int64_t>(&v.data)) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), *i);
        out.append(buf, static_cast<size_t>(ptr - buf));
    } else if (auto* u = std::get_if<uint64_t>(&v.data)) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), *u);
        out.append(buf, static_cast<size_t>(ptr - buf));
    } else if (auto* n = std::get_if<double>(&v.data)) {
        if (!std::isfinite(*n)) {
            out += "null";
        } else {
            char buf[64];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), *n);
            if (ec == std::errc{}) {
                out.append(buf, static_cast<size_t>(ptr - buf));
            } else {
                out += "null";
            }
        }
    } else if (auto* s = std::get_if<std::string>(&v.data)) {
        append_escaped(out, *s);
    } else if (auto* a = std::get_if<JsonArr>(&v.data)) {
        out.push_back('[');
        for (size_t idx = 0; idx < a->size(); ++idx) {
            if (idx > 0) out.push_back(',');
            stringify_into((*a)[idx], out);
        }
        out.push_back(']');
    } else if (auto* o = std::get_if<JsonObj>(&v.data)) {
        out.push_back('{');
        for (size_t idx = 0; idx < o->size(); ++idx) {
            if (idx > 0) out.push_back(',');
            append_escaped(out, (*o)[idx].first);
            out.push_back(':');
            stringify_into((*o)[idx].second, out);
        }
        out.push_back('}');
    } else {
        out += "null";
    }
}

[[nodiscard]] inline std::string stringify(const JsonVal& v) {
    std::string out;
    out.reserve(512);
    stringify_into(v, out);
    return out;
}

struct Parser {
    static constexpr size_t MAX_DEPTH = 128;
    std::string_view s;
    size_t pos = 0;
    size_t depth = 0;

    inline void ws() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) pos++;
    }
    inline char peek() {
        ws();
        if (pos >= s.size()) throw std::runtime_error("unexpected EOF in json");
        return s[pos];
    }
    inline char next() {
        if (pos >= s.size()) throw std::runtime_error("unexpected EOF in json");
        return s[pos++];
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string parse_str() {
        if (next() != '"') throw std::runtime_error("expected '\"'");
        std::string out;
        while (true) {
            size_t start = pos;
            while (pos < s.size() && s[pos] != '"' && s[pos] != '\\') {
                if (static_cast<unsigned char>(s[pos]) < 0x20) {
                    throw std::runtime_error("unescaped control char in json string");
                }
                pos++;
            }
            if (pos > start) out.append(s.data() + start, pos - start);
            char c = next();
            if (c == '"') break;
            char e = next();
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > s.size()) throw std::runtime_error("bad \\u escape");
                    uint32_t code = 0;
                    auto [p1, ec1] = std::from_chars(s.data() + pos, s.data() + pos + 4, code, 16);
                    if (ec1 != std::errc{} || p1 != s.data() + pos + 4) {
                        throw std::runtime_error("invalid \\u hex escape");
                    }
                    pos += 4;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos + 6 > s.size() || s[pos] != '\\' || s[pos + 1] != 'u') {
                            throw std::runtime_error("unpaired high surrogate in json");
                        }
                        uint32_t code2 = 0;
                        auto [p2, ec2] = std::from_chars(s.data() + pos + 2, s.data() + pos + 6, code2, 16);
                        if (ec2 != std::errc{} || p2 != s.data() + pos + 6 || code2 < 0xDC00 || code2 > 0xDFFF) {
                            throw std::runtime_error("invalid low surrogate in json");
                        }
                        pos += 6;
                        code = 0x10000 + (((code - 0xD800) << 10) | (code2 - 0xDC00));
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        throw std::runtime_error("unpaired low surrogate in json");
                    }
                    append_utf8(out, code);
                    break;
                }
                default:
                    throw std::runtime_error("invalid escape sequence in json string");
            }
        }
        return out;
    }

    JsonVal parse_val() {
        if (depth >= MAX_DEPTH) throw std::runtime_error("json max nesting depth exceeded");
        char c = peek();
        std::string_view rem = s.substr(pos);
        if (c == 'n') {
            if (rem.starts_with("null")) { pos += 4; return JsonVal(nullptr); }
            throw std::runtime_error("invalid json literal");
        }
        if (c == 't') {
            if (rem.starts_with("true")) { pos += 4; return JsonVal(true); }
            throw std::runtime_error("invalid json literal");
        }
        if (c == 'f') {
            if (rem.starts_with("false")) { pos += 5; return JsonVal(false); }
            throw std::runtime_error("invalid json literal");
        }
        if (c == '"') return JsonVal(parse_str());
        if (c == '[') {
            pos++;
            depth++;
            struct DepthPop { size_t& d; ~DepthPop() { --d; } } pop{depth};
            JsonArr arr;
            if (peek() == ']') { pos++; return JsonVal(std::move(arr)); }
            while (true) {
                arr.push_back(parse_val());
                char sep = peek();
                if (sep == ',') { pos++; continue; }
                if (sep == ']') { pos++; break; }
                throw std::runtime_error("expected ',' or ']'");
            }
            return JsonVal(std::move(arr));
        }
        if (c == '{') {
            pos++;
            depth++;
            struct DepthPop { size_t& d; ~DepthPop() { --d; } } pop{depth};
            JsonObj obj;
            if (peek() == '}') { pos++; return JsonVal(std::move(obj)); }
            while (true) {
                ws();
                std::string key = parse_str();
                if (peek() != ':') throw std::runtime_error("expected ':'");
                pos++;
                JsonVal val = parse_val();
                obj.emplace_back(std::move(key), std::move(val));
                char sep = peek();
                if (sep == ',') { pos++; continue; }
                if (sep == '}') { pos++; break; }
                throw std::runtime_error("expected ',' or '}'");
            }
            return JsonVal(std::move(obj));
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = pos;
            bool is_float = false;
            if (s[pos] == '-') pos++;
            if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
                throw std::runtime_error("invalid json number");
            }
            if (s[pos] == '0') {
                pos++;
                if (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                    throw std::runtime_error("leading zero in json number");
                }
            } else {
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            if (pos < s.size() && s[pos] == '.') {
                is_float = true;
                pos++;
                if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
                    throw std::runtime_error("invalid fraction in json number");
                }
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                is_float = true;
                pos++;
                if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
                if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
                    throw std::runtime_error("invalid exponent in json number");
                }
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }

            std::string_view tok = s.substr(start, pos - start);
            if (!is_float) {
                int64_t ival = 0;
                auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), ival);
                if (ec == std::errc{} && ptr == tok.data() + tok.size()) {
                    return JsonVal(ival);
                }
                if (tok[0] != '-') {
                    uint64_t uval = 0;
                    auto [uptr, uec] = std::from_chars(tok.data(), tok.data() + tok.size(), uval);
                    if (uec == std::errc{} && uptr == tok.data() + tok.size()) {
                        return JsonVal(uval);
                    }
                }
            }
            return JsonVal(num::parse_double_strict(tok));
        }
        throw std::runtime_error("unexpected char in json");
    }
};

[[nodiscard]] inline JsonVal parse(std::string_view s) {
    Parser p{s, 0};
    JsonVal res = p.parse_val();
    p.ws();
    if (p.pos != p.s.size()) {
        throw std::runtime_error("trailing data after json value");
    }
    return res;
}

} // namespace seb::json