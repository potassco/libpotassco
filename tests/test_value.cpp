//
// Copyright (c) 2017 - present, Benjamin Kaufmann
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
#include <potassco/program_opts/typed_value.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <map>
#include <memory>

namespace Potassco::ProgramOptions::Test {
namespace Po   = ProgramOptions;
using ValuePtr = std::unique_ptr<Value>;

TEST_CASE("Test flag", "[value]") {
    bool loud;
    SECTION("check properties") {
        ValuePtr loudFlag(Po::flag(loud));
        REQUIRE(loudFlag->isImplicit() == true);
        REQUIRE(loudFlag->isFlag() == true);
        REQUIRE(strcmp(loudFlag->implicit(), "1") == 0);
    }
    SECTION("default parser stores true") {
        ValuePtr loudFlag(Po::flag(loud));
        REQUIRE((loudFlag->parse("", "") && loud == true));
        loud = false;
        loudFlag->parse("", "on");
        REQUIRE(loud == true);
    }
    SECTION("alternative parser can store false") {
        ValuePtr quietFlag(Po::flag(loud, Po::store_false));
        REQUIRE((quietFlag->parse("", "") && loud == false));
        quietFlag->parse("", "off");
        REQUIRE(loud == true);
    }
}

TEST_CASE("Test storeTo", "[value]") {
    int      x;
    bool     y;
    ValuePtr v1(Po::storeTo(x));
    ValuePtr v2(Po::flag(y));
    SECTION("store int") {
        REQUIRE(v1->parse("", "22"));
        REQUIRE(x == 22);
    }
    SECTION("fail on invalid type") {
        x = 99;
        REQUIRE(!v1->parse("", "ab"));
        REQUIRE(x == 99);
    }
    SECTION("init with state") {
        ValuePtr v(Po::storeTo(x)->state(Po::Value::value_defaulted));
        REQUIRE(v->state() == Po::Value::value_defaulted);
        REQUIRE((v2->state() == Po::Value::value_unassigned && v2->isImplicit() && v2->isFlag()));
    }
    SECTION("parse as default") {
        REQUIRE(v2->parse("", "off", Po::Value::value_defaulted));
        REQUIRE(v2->state() == Po::Value::value_defaulted);
    }
    SECTION("parse bool as implicit") {
        REQUIRE(v2->parse("", ""));
        REQUIRE(y == true);
        v2->implicit("0");
        REQUIRE(v2->parse("", ""));
        REQUIRE(y == false);
    }
    SECTION("parse int as implicit") {
        v1->implicit(POTASSCO_STRING(102));
        REQUIRE(v1->isImplicit());
        REQUIRE((v1->parse("", "") && x == 102));
    }

    SECTION("test custom parser") {
        bool     parsed = false;
        ValuePtr vc(Po::storeTo(
                        parsed,
                        +[](const std::string&, bool& p) {
                            p = true;
                            return true;
                        })
                        ->implicit(""));
        REQUIRE(vc->parse("", ""));
        REQUIRE(parsed);
    }
}

TEST_CASE("Test action value", "[value]") {
    std::map<std::string, int> m;
    ValuePtr                   v1(Po::action<int>([&](const std::string& name, int v) { m[name] = v; }));
    ValuePtr                   v2(Po::action<int>([&](int v) { m["v2"] = v; }));
    CHECK(v1->parse("foo", "123"));
    CHECK(v1->parse("bar", "342"));
    CHECK(v2->parse("jojo", "999"));
    REQUIRE(m.size() == 3);
    REQUIRE(m["foo"] == 123);
    REQUIRE(m["bar"] == 342);
    REQUIRE(m["v2"] == 999);
}
TEST_CASE("Test custom value", "[value]") {
    std::map<std::string, int> m;
    auto                       parser = [&](const std::string& name, const std::string& v) {
        if (int temp; Potassco::string_cast<int>(v, temp)) {
            m[name] = temp;
            return true;
        }
        return false;
    };
    ValuePtr v1(Po::parse(parser));
    ValuePtr v2(Po::parse([&](const std::string& v) { return parser("v2", v); }));
    REQUIRE(v1->parse("foo", "123"));
    REQUIRE(v2->parse("", "342"));
    REQUIRE(v1->parse("jojo", "999"));
    REQUIRE_FALSE(v1->parse("kaputt", "x12"));
    REQUIRE(m.size() == 3);
    REQUIRE(m["foo"] == 123);
    REQUIRE(m["jojo"] == 999);
    REQUIRE(m["v2"] == 342);
}

enum class Color { RED = 2, GREEN = 10, BLUE = 20 };
enum class Mode { DEF, IMP, EXP };

TEST_CASE("Test enum value", "[value]") {
    int  x;
    Mode y;

    ValuePtr v1(Po::storeTo(x, Po::values<Color>({
                                   {"Red", Color::RED},
                                   {"Green", Color::GREEN},
                                   {"Blue", Color::BLUE},
                               })));

    ValuePtr v2(Po::storeTo(y, Po::values<Mode>({
                                   {"Default", Mode::DEF},
                                   {"Implicit", Mode::IMP},
                                   {"Explicit", Mode::EXP},
                               })));

    REQUIRE((v1->parse("", "Red") && x == 2));
    REQUIRE((v1->parse("", "GREEN") && x == (int) Color::GREEN));
    REQUIRE(!v1->parse("", "Blu"));

    REQUIRE((v2->parse("", "Implicit") && y == Mode::IMP));
}
} // namespace Potassco::ProgramOptions::Test
