#include "Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace opera {

// ---------------------------------------------------------------- writer

void JsonWriter::beforeValue() {
    if (afterKey_) {          // the key already placed the comma
        afterKey_ = false;
        return;
    }
    if (!firstInContainer_.empty()) {
        if (!firstInContainer_.back()) {
            out_ += ',';
        }
        firstInContainer_.back() = false;
    }
}

JsonWriter& JsonWriter::beginObject() {
    beforeValue();
    out_ += '{';
    firstInContainer_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endObject() {
    out_ += '}';
    firstInContainer_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::beginArray() {
    beforeValue();
    out_ += '[';
    firstInContainer_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endArray() {
    out_ += ']';
    firstInContainer_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(const std::string& name) {
    beforeValue();
    appendEscaped(name);
    out_ += ':';
    afterKey_ = true;
    return *this;
}

void JsonWriter::appendEscaped(const std::string& s) {
    out_ += '"';
    for (const char c : s) {
        switch (c) {
            case '"':  out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out_ += buf;
                } else {
                    out_ += c;  // UTF-8 bytes pass through unchanged
                }
        }
    }
    out_ += '"';
}

JsonWriter& JsonWriter::value(const std::string& s) {
    beforeValue();
    appendEscaped(s);
    return *this;
}

JsonWriter& JsonWriter::value(const char* s) { return value(std::string(s)); }

JsonWriter& JsonWriter::value(int n) {
    beforeValue();
    out_ += std::to_string(n);
    return *this;
}

JsonWriter& JsonWriter::value(double d) {
    beforeValue();
    if (!std::isfinite(d)) {
        out_ += "null";  // JSON has no infinity; callers should not send it anyway
        return *this;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", d);
    out_ += buf;
    return *this;
}

JsonWriter& JsonWriter::value(bool b) {
    beforeValue();
    out_ += b ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::null() {
    beforeValue();
    out_ += "null";
    return *this;
}

// ---------------------------------------------------------------- reader

// Recursive descent over one flat object. Kept as a class so the position
// and error message travel together instead of through every function.
class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool parseObject(JsonObject& out, std::string& error) {
        skipSpace();
        if (!eat('{')) return failWith(error, "expected '{' at the start");
        skipSpace();
        if (eat('}')) return finish(error);
        while (true) {
            skipSpace();
            std::string key;
            if (!parseString(key)) return failWith(error, "expected a quoted key");
            skipSpace();
            if (!eat(':')) return failWith(error, "expected ':' after key \"" + key + "\"");
            skipSpace();
            JsonObject::Value v;
            if (!parseValue(v, error)) return false;
            out.fields_[key] = v;
            skipSpace();
            if (eat(',')) continue;
            if (eat('}')) return finish(error);
            return failWith(error, "expected ',' or '}'");
        }
    }

private:
    bool parseValue(JsonObject::Value& v, std::string& error) {
        if (pos_ >= s_.size()) return failWith(error, "unexpected end of input");
        const char c = s_[pos_];
        if (c == '"') {
            v.kind = JsonObject::Value::Kind::String;
            return parseString(v.text) || failWith(error, "bad string value");
        }
        if (c == '{' || c == '[') {
            return failWith(error, "nested objects and arrays are not accepted");
        }
        if (matchWord("true"))  { v.kind = JsonObject::Value::Kind::Bool; v.flag = true;  return true; }
        if (matchWord("false")) { v.kind = JsonObject::Value::Kind::Bool; v.flag = false; return true; }
        if (matchWord("null"))  { v.kind = JsonObject::Value::Kind::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* begin = s_.c_str() + pos_;
            char* end = nullptr;
            const double d = std::strtod(begin, &end);
            if (end == begin || !std::isfinite(d)) return failWith(error, "bad number");
            for (const char* p = begin; p != end; ++p) {  // strtod also takes hex: refuse it
                const char ch = *p;
                if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' ||
                      ch == 'e' || ch == 'E')) {
                    return failWith(error, "bad number");
                }
            }
            pos_ += static_cast<std::size_t>(end - begin);
            v.kind = JsonObject::Value::Kind::Number;
            v.number = d;
            return true;
        }
        return failWith(error, std::string("unexpected character '") + c + "'");
    }

    bool parseString(std::string& out) {
        if (!eat('"')) return false;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;  // raw control char
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) return false;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return false;
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[pos_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    if (code >= 0xD800 && code <= 0xDFFF) return false;  // surrogate pairs not needed
                    appendUtf8(out, code);
                    break;
                }
                default: return false;
            }
        }
        return false;  // no closing quote
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool matchWord(const char* w) {
        std::size_t n = 0;
        while (w[n] != '\0') ++n;
        if (s_.compare(pos_, n, w) != 0) return false;
        pos_ += n;
        return true;
    }

    bool finish(std::string& error) {
        skipSpace();
        if (pos_ != s_.size()) return failWith(error, "unexpected text after the object");
        return true;
    }

    bool failWith(std::string& error, const std::string& message) {
        error = message + " (at character " + std::to_string(pos_) + ")";
        return false;
    }

    void skipSpace() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool eat(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

bool JsonObject::parse(const std::string& text, JsonObject& out, std::string& error) {
    out.fields_.clear();
    JsonParser p(text);
    return p.parseObject(out, error);
}

bool JsonObject::getInt(const std::string& key, int& out) const {
    const auto it = fields_.find(key);
    if (it == fields_.end() || it->second.kind != Value::Kind::Number) return false;
    const double d = it->second.number;
    if (d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0) return false;
    out = static_cast<int>(d);
    return true;
}

bool JsonObject::getString(const std::string& key, std::string& out) const {
    const auto it = fields_.find(key);
    if (it == fields_.end() || it->second.kind != Value::Kind::String) return false;
    out = it->second.text;
    return true;
}

bool JsonObject::getBool(const std::string& key, bool& out) const {
    const auto it = fields_.find(key);
    if (it == fields_.end() || it->second.kind != Value::Kind::Bool) return false;
    out = it->second.flag;
    return true;
}

}  // namespace opera
