#pragma once

#include "win_crypto.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace seb::plist {

struct PlistValue;
using PlistDict = std::vector<std::pair<std::string, PlistValue>>;
using PlistArray = std::vector<PlistValue>;

struct PlistData {
    std::vector<uint8_t> bytes;
};
struct PlistDate {
    std::string iso;
};
struct PlistStr {
    std::optional<std::string> text;
};

struct PlistValue {
    std::variant<PlistDict, PlistArray, PlistStr, int64_t, double, bool, PlistData, PlistDate> data;
};

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string unescape_xml(std::string_view s) {
    if (s.find('&') == std::string_view::npos) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i);
            if (semi != std::string_view::npos) {
                std::string_view ent = s.substr(i + 1, semi - i - 1);
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "apos") { out.push_back('\''); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    uint32_t cp = 0;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                        std::from_chars(ent.data() + 2, ent.data() + ent.size(), cp, 16);
                    } else {
                        std::from_chars(ent.data() + 1, ent.data() + ent.size(), cp, 10);
                    }
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
                    i = semi;
                    continue;
                }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

struct XmlParser {
    static constexpr size_t MAX_DEPTH = 128;
    std::string_view src;
    size_t pos = 0;
    size_t depth = 0;

    inline void skip_ws() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) pos++;
    }

    inline bool starts_with(std::string_view pat) const {
        return src.size() - pos >= pat.size() && src.compare(pos, pat.size(), pat) == 0;
    }

    void skip_prolog() {
        while (true) {
            skip_ws();
            if (pos >= src.size()) break;
            if (starts_with("<?")) {
                size_t end = src.find("?>", pos);
                if (end == std::string_view::npos) throw std::runtime_error("unterminated <?");
                pos = end + 2;
            } else if (starts_with("<!DOCTYPE")) {
                size_t end = src.find('>', pos);
                if (end == std::string_view::npos) throw std::runtime_error("unterminated DOCTYPE");
                pos = end + 1;
            } else if (starts_with("<!--")) {
                size_t end = src.find("-->", pos);
                if (end == std::string_view::npos) throw std::runtime_error("unterminated comment");
                pos = end + 3;
            } else {
                break;
            }
        }
    }

    struct Tag {
        std::string_view name;
        bool is_close;
        bool self_close;
    };

    Tag read_tag() {
        skip_prolog();
        if (pos >= src.size() || src[pos] != '<') throw std::runtime_error("expected '<'");
        pos++;
        bool is_close = false;
        if (pos < src.size() && src[pos] == '/') {
            is_close = true;
            pos++;
        }
        size_t end = src.find('>', pos);
        if (end == std::string_view::npos) throw std::runtime_error("unterminated tag");
        std::string_view raw = src.substr(pos, end - pos);
        pos = end + 1;
        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
        bool self_close = !is_close && !raw.empty() && raw.back() == '/';
        if (self_close) {
            raw.remove_suffix(1);
            while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
        }
        size_t sp = 0;
        while (sp < raw.size() && !std::isspace(static_cast<unsigned char>(raw[sp]))) sp++;
        std::string_view name = raw.substr(0, sp);
        size_t colon = name.rfind(':');
        if (colon != std::string_view::npos) name = name.substr(colon + 1);
        return {name, is_close, self_close};
    }

    std::optional<std::string_view> read_raw_until_close(std::string_view expected) {
        size_t start = pos;
        size_t lt = src.find('<', pos);
        if (lt == std::string_view::npos) throw std::runtime_error("expected closing tag");
        std::string_view raw = src.substr(start, lt - start);
        pos = lt;
        Tag t = read_tag();
        if (!t.is_close || t.name != expected) {
            throw std::runtime_error("expected </" + std::string(expected) + ">");
        }
        if (raw.empty()) return std::nullopt;
        return raw;
    }

    PlistValue parse_value() {
        if (depth >= MAX_DEPTH) throw std::runtime_error("plist max nesting depth exceeded");
        Tag t = read_tag();
        if (t.is_close) throw std::runtime_error("unexpected closing tag </" + std::string(t.name) + ">");
        if (t.name == "true") {
            if (!t.self_close) read_raw_until_close("true");
            return PlistValue{true};
        }
        if (t.name == "false") {
            if (!t.self_close) read_raw_until_close("false");
            return PlistValue{false};
        }
        if (t.name == "string") {
            if (t.self_close) return PlistValue{PlistStr{std::nullopt}};
            auto raw = read_raw_until_close("string");
            if (!raw) return PlistValue{PlistStr{std::nullopt}};
            return PlistValue{PlistStr{unescape_xml(*raw)}};
        }
        if (t.name == "integer") {
            auto raw = t.self_close ? std::nullopt : read_raw_until_close("integer");
            int64_t val = 0;
            if (raw && !raw->empty()) {
                std::string_view sv = *raw;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
                if (ec != std::errc{}) val = std::stoll(std::string(sv));
            }
            return PlistValue{val};
        }
        if (t.name == "real") {
            auto raw = t.self_close ? std::nullopt : read_raw_until_close("real");
            double val = 0.0;
            if (raw && !raw->empty()) {
                std::string_view sv = *raw;
                if (sv.size() < 64) {
                    char buf[64];
                    std::memcpy(buf, sv.data(), sv.size());
                    buf[sv.size()] = '\0';
                    val = std::strtod(buf, nullptr);
                } else {
                    val = std::stod(std::string(sv));
                }
            }
            return PlistValue{val};
        }
        if (t.name == "data") {
            auto raw = t.self_close ? std::nullopt : read_raw_until_close("data");
            return PlistValue{PlistData{raw ? win::base64_decode(*raw) : std::vector<uint8_t>{}}};
        }
        if (t.name == "date") {
            auto raw = t.self_close ? std::nullopt : read_raw_until_close("date");
            return PlistValue{PlistDate{win::format_plist_date(raw.value_or(""))}};
        }
        if (t.name == "dict") {
            if (t.self_close) return PlistValue{PlistDict{}};
            depth++;
            struct DepthPop { size_t& d; ~DepthPop() { --d; } } pop{depth};
            PlistDict entries;
            while (true) {
                skip_prolog();
                if (starts_with("</dict>")) {
                    read_tag();
                    break;
                }
                Tag kt = read_tag();
                if (kt.is_close || kt.name != "key") throw std::runtime_error("expected <key>");
                std::string key;
                if (!kt.self_close) {
                    if (auto raw = read_raw_until_close("key")) {
                        key = unescape_xml(*raw);
                    }
                }
                PlistValue val = parse_value();
                entries.emplace_back(std::move(key), std::move(val));
            }
            return PlistValue{std::move(entries)};
        }
        if (t.name == "array") {
            if (t.self_close) return PlistValue{PlistArray{}};
            depth++;
            struct DepthPop { size_t& d; ~DepthPop() { --d; } } pop{depth};
            PlistArray items;
            while (true) {
                skip_prolog();
                if (starts_with("</array>")) {
                    read_tag();
                    break;
                }
                items.push_back(parse_value());
            }
            return PlistValue{std::move(items)};
        }
        throw std::runtime_error("unsupported plist element: " + std::string(t.name));
    }
};

inline PlistDict load_settings_xml_bytes(const std::vector<uint8_t>& data) {
    size_t offset = (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) ? 3 : 0;
    std::string_view xml(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);
    XmlParser p{xml, 0};
    auto t = p.read_tag();
    if (t.name != "plist") throw std::runtime_error("expected <plist>");
    PlistValue root = p.parse_value();
    auto* d = std::get_if<PlistDict>(&root.data);
    if (!d) throw std::runtime_error("expected root dictionary");
    return std::move(*d);
}

inline void append_dotnet_double(double v, std::string& out) {
    if (std::isnan(v)) { out += "NaN"; return; }
    if (std::isinf(v)) { out += (v > 0 ? "Infinity" : "-Infinity"); return; }
    if (v == 0.0) { out += (std::signbit(v) ? "-0" : "0"); return; }
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    std::string_view s(buf, static_cast<size_t>(ptr - buf));
    if (s.size() >= 2 && s.substr(s.size() - 2) == ".0") {
        s.remove_suffix(2);
    }
    size_t ep = s.find('e');
    if (ep != std::string_view::npos) {
        out.append(s.substr(0, ep));
        int exp = 0;
        std::from_chars(s.data() + ep + 1, s.data() + s.size(), exp);
        char ebuf[16];
        int n = std::snprintf(ebuf, sizeof(ebuf), "E%c%02d", exp < 0 ? '-' : '+', std::abs(exp));
        if (n > 0) out.append(ebuf, static_cast<size_t>(n));
    } else {
        out.append(s);
    }
}

inline void serialize_value(const PlistValue& v, std::string& out) {
    if (auto* d = std::get_if<PlistDict>(&v.data)) {
        std::vector<const std::pair<std::string, PlistValue>*> sorted;
        sorted.reserve(d->size());
        for (const auto& kv : *d) sorted.push_back(&kv);
        std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
            return win::compare_invariant(a->first, b->first) < 0;
        });
        out.push_back('{');
        bool first = true;
        for (size_t i = 0; i < sorted.size(); ++i) {
            const auto& [k, val] = *sorted[i];
            const auto* sub_d = std::get_if<PlistDict>(&val.data);
            if (iequals(k, "originatorversion") || (sub_d && sub_d->empty())) {
                continue;
            }
            if (!first) out.push_back(',');
            first = false;
            out.push_back('"');
            out += k;
            out += "\":";
            serialize_value(val, out);
        }
        out.push_back('}');
    } else if (auto* a = std::get_if<PlistArray>(&v.data)) {
        out.push_back('[');
        for (size_t i = 0; i < a->size(); ++i) {
            if (i > 0) out.push_back(',');
            serialize_value((*a)[i], out);
        }
        out.push_back(']');
    } else if (auto* bd = std::get_if<PlistData>(&v.data)) {
        out.push_back('"');
        out += win::base64_encode(bd->bytes.data(), bd->bytes.size());
        out.push_back('"');
    } else if (auto* dt = std::get_if<PlistDate>(&v.data)) {
        out.push_back('"');
        out += dt->iso;
        out.push_back('"');
    } else if (auto* s = std::get_if<PlistStr>(&v.data)) {
        if (!s->text.has_value()) {
            out += "\"\"";
        } else {
            out.push_back('"');
            out += *s->text;
            out.push_back('"');
        }
    } else if (auto* b = std::get_if<bool>(&v.data)) {
        out += (*b ? "true" : "false");
    } else if (auto* n = std::get_if<int64_t>(&v.data)) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), *n);
        out.append(buf, static_cast<size_t>(ptr - buf));
    } else if (auto* f = std::get_if<double>(&v.data)) {
        append_dotnet_double(*f, out);
    }
}

inline std::string exam_configuration_key(const PlistDict& settings, std::string_view purpose) {
    PlistDict effective = settings;
    if (purpose != "as-is") {
        int64_t pval = (purpose == "starting-exam") ? 0 : 1;
        bool found = false;
        for (auto& [k, v] : effective) {
            if (k == "sebConfigPurpose") {
                v = PlistValue{pval};
                found = true;
                break;
            }
        }
        if (!found) {
            effective.emplace_back("sebConfigPurpose", PlistValue{pval});
        }
    }
    std::string serialized;
    serialized.reserve(4096);
    serialize_value(PlistValue{std::move(effective)}, serialized);
    return win::sha256_hex(serialized);
}

inline const PlistValue* find_key(const PlistDict& dict, std::string_view key) {
    for (const auto& [k, v] : dict) {
        if (k == key) return &v;
    }
    return nullptr;
}

} // namespace seb::plist
