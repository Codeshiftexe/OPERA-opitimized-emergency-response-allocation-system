#pragma once

// Minimal JSON support for the HTTP API.
//
//   JsonWriter  builds any JSON document (objects, arrays, strings, numbers).
//   JsonObject  parses ONE flat object whose values are strings, numbers,
//               booleans or null - e.g. {"type":"Cardiac","node":3}.
//
// The API only ever receives flat objects like that, so a full JSON library
// is not needed. Nested objects or arrays in a request are rejected with an
// error instead of being half-understood.

#include <string>
#include <unordered_map>
#include <vector>

namespace opera {

class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();

    // Inside an object: name of the next value.
    JsonWriter& key(const std::string& name);

    JsonWriter& value(const std::string& s);
    JsonWriter& value(const char* s);
    JsonWriter& value(int n);
    JsonWriter& value(double d);
    JsonWriter& value(bool b);
    JsonWriter& null();

    const std::string& str() const { return out_; }

private:
    void beforeValue();
    void appendEscaped(const std::string& s);

    std::string out_;
    std::vector<bool> firstInContainer_;  // one entry per open { or [
    bool afterKey_ = false;
};

class JsonObject {
public:
    // Returns false and sets `error` if `text` is not one flat JSON object.
    static bool parse(const std::string& text, JsonObject& out, std::string& error);

    bool has(const std::string& key) const { return fields_.count(key) != 0; }

    // Each getter returns false if the key is missing or has another type.
    // getInt also refuses numbers with a fractional part.
    bool getInt(const std::string& key, int& out) const;
    bool getString(const std::string& key, std::string& out) const;
    bool getBool(const std::string& key, bool& out) const;

private:
    struct Value {
        enum class Kind { String, Number, Bool, Null } kind = Kind::Null;
        std::string text;
        double number = 0.0;
        bool flag = false;
    };
    std::unordered_map<std::string, Value> fields_;

    friend class JsonParser;
};

}  // namespace opera
