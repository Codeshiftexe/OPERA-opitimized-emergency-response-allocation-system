// Tests for JsonWriter and the flat JsonObject reader.

#include <string>

#include "Json.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

void testWriterShapes() {
    JsonWriter w;
    w.beginObject()
        .key("a").value(1)
        .key("b").beginArray().value(true).value(false).null().endArray()
        .key("c").beginObject().key("d").value("x").endObject()
        .key("e").beginArray().endArray()
        .endObject();
    CHECK_EQ(w.str(), std::string(R"({"a":1,"b":[true,false,null],"c":{"d":"x"},"e":[]})"));
}

void testWriterEscapingAndNumbers() {
    JsonWriter w;
    w.beginArray()
        .value("quote\" back\\ line\n tab\t")
        .value(std::string(1, '\x01'))
        .value(14.0)
        .value(2.5)
        .value(-3)
        .value(1.0 / 0.0)   // infinity is not JSON: written as null
        .endArray();
    CHECK_EQ(w.str(), std::string(R"(["quote\" back\\ line\n tab\t","\u0001",14,2.5,-3,null])"));
}

void testParseFlatObject() {
    JsonObject o;
    std::string err;
    CHECK(JsonObject::parse(R"( { "type" : "Cardiac", "node": 3, "online": false, "x": null } )", o, err));
    std::string type;
    int node = -1;
    bool online = true;
    CHECK(o.getString("type", type));
    CHECK_EQ(type, std::string("Cardiac"));
    CHECK(o.getInt("node", node));
    CHECK_EQ(node, 3);
    CHECK(o.getBool("online", online));
    CHECK(!online);
    CHECK(o.has("x"));
    CHECK(!o.has("missing"));

    CHECK(JsonObject::parse("{}", o, err));
    CHECK(!o.has("type"));
}

void testGettersAreTypeStrict() {
    JsonObject o;
    std::string err;
    CHECK(JsonObject::parse(R"({"n":"3","f":2.5,"b":1,"big":1e12})", o, err));
    int i = 0;
    bool b = false;
    std::string s;
    CHECK(!o.getInt("n", i));      // a string is not a number
    CHECK(!o.getInt("f", i));      // 2.5 is not a whole number
    CHECK(!o.getBool("b", b));     // 1 is not a boolean
    CHECK(!o.getInt("big", i));    // does not fit in int
    CHECK(!o.getString("f", s));
}

void testParseEscapes() {
    JsonObject o;
    std::string err;
    CHECK(JsonObject::parse(R"({"s":"a\"b\\c\/d\n\u00e9"})", o, err));
    std::string s;
    CHECK(o.getString("s", s));
    CHECK_EQ(s, std::string("a\"b\\c/d\n\xC3\xA9"));  // \u00e9 -> UTF-8 for e-acute
}

void testRejectsBadInput() {
    JsonObject o;
    std::string err;
    CHECK(!JsonObject::parse("", o, err));
    CHECK(!JsonObject::parse("[1,2]", o, err));
    CHECK(!JsonObject::parse(R"({"a":{"b":1}})", o, err));
    CHECK(err.find("nested") != std::string::npos);
    CHECK(!JsonObject::parse(R"({"a":[1]})", o, err));
    CHECK(!JsonObject::parse(R"({"a":1,})", o, err));
    CHECK(!JsonObject::parse(R"({"a":1} extra)", o, err));
    CHECK(!JsonObject::parse(R"({"a":0x10})", o, err));
    CHECK(!JsonObject::parse(R"({"a":tru})", o, err));
    CHECK(!JsonObject::parse(R"({"a":"unterminated})", o, err));
    CHECK(!JsonObject::parse("{a:1}", o, err));
}

}  // namespace

int main() {
    testWriterShapes();
    testWriterEscapingAndNumbers();
    testParseFlatObject();
    testGettersAreTypeStrict();
    testParseEscapes();
    testRejectsBadInput();
    return report("test_json");
}
