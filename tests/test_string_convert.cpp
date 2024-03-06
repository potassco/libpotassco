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
#include "catch.hpp"

#include <potassco/string_convert.h>

#include <sstream>
#include <string>
#include <vector>

namespace Potassco::Test {

TEST_CASE("String conversion", "[string]") {
    errno = 0;
#define REQUIRE_CONV_FAIL(X, Y) REQUIRE((X).ec == Y)

    SECTION("empty string is not an int") {
        REQUIRE_THROWS(Potassco::string_cast<int>(""));
        REQUIRE_THROWS(Potassco::string_cast<unsigned>(""));
        int      iVal;
        unsigned uVal;
        REQUIRE_CONV_FAIL(Potassco::fromChars("", iVal), std::errc::invalid_argument);
        REQUIRE_CONV_FAIL(Potassco::fromChars("", uVal), std::errc::invalid_argument);
    }
    SECTION("at least one digit") {
        REQUIRE_THROWS(Potassco::string_cast<int>("+"));
        REQUIRE_THROWS(Potassco::string_cast<unsigned>("+"));
        int      iVal;
        unsigned uVal;
        REQUIRE_CONV_FAIL(Potassco::fromChars("+", iVal), std::errc::invalid_argument);
        REQUIRE_CONV_FAIL(Potassco::fromChars("+", uVal), std::errc::invalid_argument);
    }
    SECTION("overflow is an error") {
        REQUIRE_THROWS(Potassco::string_cast<int64_t>("18446744073709551616"));
        REQUIRE_THROWS(Potassco::string_cast<uint64_t>("18446744073709551616"));
        int64_t  iVal;
        uint64_t uVal;
        REQUIRE_CONV_FAIL(Potassco::fromChars("18446744073709551616", iVal), std::errc::result_out_of_range);
        REQUIRE_CONV_FAIL(Potassco::fromChars("18446744073709551616", uVal), std::errc::result_out_of_range);
    }
    SECTION("positive and negative ints convert to string") {
        REQUIRE(Potassco::string_cast(10) == "10");
        REQUIRE(Potassco::string_cast(-10) == "-10");
    }
    SECTION("unsigned -1 converts to named limit") {
        REQUIRE(Potassco::string_cast(static_cast<unsigned int>(-1)) == "umax");
        REQUIRE(Potassco::string_cast(static_cast<unsigned long>(-1)) == "umax");
        REQUIRE(Potassco::string_cast<unsigned int>("umax") == static_cast<unsigned int>(-1));
        REQUIRE(Potassco::string_cast<unsigned long>("umax") == static_cast<unsigned long>(-1));
        REQUIRE(Potassco::string_cast<unsigned long long>("umax") == static_cast<unsigned long long>(-1));
        REQUIRE(Potassco::string_cast<uint64_t>("umax") == static_cast<uint64_t>(-1));
    }
    SECTION("-1 is only signed value accepted as unsigned") {
        REQUIRE(Potassco::string_cast<unsigned int>("-1") == static_cast<unsigned int>(-1));
        REQUIRE_THROWS_AS(Potassco::string_cast<unsigned long long>("-2"), Potassco::bad_string_cast);
    }
    SECTION("umax does not apply to signed int") {
        REQUIRE_THROWS_AS(Potassco::string_cast<int>("umax"), Potassco::bad_string_cast);
        REQUIRE_THROWS_AS(Potassco::string_cast<long>("umax"), Potassco::bad_string_cast);
        REQUIRE_THROWS_AS(Potassco::string_cast<long long>("umax"), Potassco::bad_string_cast);
        REQUIRE_THROWS_AS(Potassco::string_cast<int64_t>("umax"), Potassco::bad_string_cast);
    }
    SECTION("named limits convert to signed ints") {
        REQUIRE(Potassco::string_cast<int>("imax") == INT_MAX);
        REQUIRE(Potassco::string_cast<int>("imin") == INT_MIN);

        REQUIRE(Potassco::string_cast<long>("imax") == LONG_MAX);
        REQUIRE(Potassco::string_cast<long>("imin") == LONG_MIN);

        REQUIRE(Potassco::string_cast<long long>("imax") == LLONG_MAX);
        REQUIRE(Potassco::string_cast<long long>("imin") == LLONG_MIN);
    }

    SECTION("chars are handled") {
        REQUIRE(Potassco::string_cast<char>("\t") == '\t');
        REQUIRE(Potassco::string_cast<char>("\r") == '\r');
        REQUIRE(Potassco::string_cast<char>("\n") == '\n');
        REQUIRE(Potassco::string_cast<char>("\v") == '\v');
        REQUIRE(Potassco::string_cast<char>("\f") == '\f');
        REQUIRE(Potassco::string_cast<char>("\a") == '\a');

        REQUIRE(Potassco::string_cast<char>("x") == 'x');
        REQUIRE(Potassco::string_cast<char>("a") == 'a');
        REQUIRE(Potassco::string_cast<char>("H") == 'H');

        REQUIRE(Potassco::string_cast<char>("49") == 49);
        REQUIRE_THROWS_AS(Potassco::string_cast<char>("256"), Potassco::bad_string_cast);
    }

    SECTION("char accepts escaped space") {
        REQUIRE(Potassco::string_cast<char>("\\t") == '\t');
        REQUIRE(Potassco::string_cast<char>("\\r") == '\r');
        REQUIRE(Potassco::string_cast<char>("\\n") == '\n');
        REQUIRE(Potassco::string_cast<char>("\\v") == '\v');
        REQUIRE(Potassco::string_cast<char>("\\f") == '\f');

        REQUIRE_THROWS_AS(Potassco::string_cast<char>("\\a"), Potassco::bad_string_cast);
    }

    SECTION("bools are handled") {
        REQUIRE(Potassco::string_cast<bool>("1"));
        REQUIRE(Potassco::string_cast<bool>("true"));
        REQUIRE(Potassco::string_cast<bool>("on"));
        REQUIRE(Potassco::string_cast<bool>("yes"));

        REQUIRE_FALSE(Potassco::string_cast<bool>("0"));
        REQUIRE_FALSE(Potassco::string_cast<bool>("false"));
        REQUIRE_FALSE(Potassco::string_cast<bool>("off"));
        REQUIRE_FALSE(Potassco::string_cast<bool>("no"));

        REQUIRE(Potassco::toString(true) == "true");
        REQUIRE(Potassco::toString(false) == "false");
    }

    SECTION("double converts to string") { REQUIRE(Potassco::string_cast(10.2) == "10.2"); }
    SECTION("double conversion is reversible") {
        const double d = 0.00000001;
        REQUIRE(Potassco::string_cast<double>(Potassco::string_cast(d)) == d);
    }
    SECTION("Pairs can be converted") {
        const std::pair<int, bool> p(10, false);
        REQUIRE(Potassco::string_cast(p) == "10,false");
        REQUIRE((Potassco::string_cast<std::pair<int, bool>>("10,false") == p));

        using IntPair = std::pair<int, int>;
        IntPair     x;
        std::string value("(1,2)");
        bool        ok = Potassco::string_cast(value, x);
        REQUIRE(ok);
        REQUIRE(x == IntPair(1, 2));
        REQUIRE(Potassco::string_cast("7", x));
        REQUIRE(x == IntPair(7, 2));

        REQUIRE(!Potassco::string_cast("9,", x));
        REQUIRE(x == IntPair(7, 2));
    }
    SECTION("Pairs can be nested") {
        using IntPair = std::pair<int, int>;
        std::pair<IntPair, IntPair> x;
        std::string                 value("((1,2),(3,4))");
        REQUIRE(Potassco::string_cast(value, x));
        REQUIRE((x.first == IntPair(1, 2) && x.second == IntPair(3, 4)));
        value = "3,4,5,6";
        REQUIRE(Potassco::string_cast(value, x));
        REQUIRE((x.first == IntPair(3, 4) && x.second == IntPair(5, 6)));
        value = "99";
        REQUIRE(Potassco::string_cast(value, x));
        REQUIRE((x.first == IntPair(99, 4) && x.second == IntPair(5, 6)));
    }
    SECTION("Sequence can be converted") {
        REQUIRE(Potassco::toString(1, 2, 3) == "1,2,3");
        REQUIRE(Potassco::toString(1, "Hallo") == "1,Hallo");

        REQUIRE(Potassco::toString(std::vector{1, 2, 3}) == "1,2,3");
    }
    SECTION("conversion works with long long") {
        long long mx = LLONG_MAX, mn = LLONG_MIN, y;
        REQUIRE((Potassco::stringTo(Potassco::toString(mx).c_str(), y) && mx == y));
        REQUIRE((Potassco::stringTo(Potassco::toString(mn).c_str(), y) && mn == y));
    }
    SECTION("conversion works with long long even if errno is initially set") {
        long long          mx  = LLONG_MAX, y;
        unsigned long long umx = ULLONG_MAX, z;
        errno                  = ERANGE;
        REQUIRE((Potassco::stringTo(Potassco::toString(mx).c_str(), y) && mx == y));

        auto s = Potassco::toString(ULLONG_MAX);
        errno  = ERANGE;
        REQUIRE((Potassco::stringTo(s.c_str(), z) && umx == z));
    }

    SECTION("double parsing is locale-independent") {
        const auto* restore = []() -> const char* {
            using P             = std::pair<std::string, std::string>;
            const char* restore = setlocale(LC_ALL, nullptr);
            if (setlocale(LC_ALL, "deu_deu"))
                return restore;
            for (const auto& [language, territory] :
                 {P("de", "DE"), P("el", "GR"), P("ru", "RU"), P("es", "ES"), P("it", "IT")}) {
                for (auto sep : {'_', '-'}) {
                    for (const auto* codeset : {"", ".utf8"}) {
                        auto loc = std::string(language).append(1, sep).append(territory).append(codeset);
                        if (setlocale(LC_ALL, loc.c_str()))
                            return restore;
                    }
                }
            }
            return nullptr;
        }();
        if (restore) {
            POTASSCO_SCOPE_EXIT({ setlocale(LC_ALL, restore); });
            REQUIRE(Potassco::string_cast<double>("12.32") == 12.32);
        }
        else {
            WARN("could not set locale - test ignored");
        }
    }

    SECTION("vectors can be converted") {
        using Vec = std::vector<int>;
        Vec         x;
        std::string value("[1,2,3,4]");
        REQUIRE(Potassco::string_cast(value, x));
        REQUIRE(x.size() == 4);
        REQUIRE(x[0] == 1);
        REQUIRE(x[1] == 2);
        REQUIRE(x[2] == 3);
        REQUIRE(x[3] == 4);
        x = Potassco::string_cast<Vec>("1,2,3");
        REQUIRE(x.size() == 3);
        REQUIRE(!Potassco::string_cast("1,2,", x));
    }
    SECTION("vectors can be nested") {
        using Vec    = std::vector<int>;
        using VecVec = std::vector<Vec>;
        VecVec      x;
        std::string value("[[1,2],[3,4]]");
        REQUIRE(Potassco::string_cast(value, x));
        REQUIRE((x.size() == 2 && x[0].size() == 2 && x[1].size() == 2));
        REQUIRE(x[0][0] == 1);
        REQUIRE(x[0][1] == 2);
        REQUIRE(x[1][0] == 3);
        REQUIRE(x[1][1] == 4);
    }
}

TEST_CASE("Format test", "[string]") {
    SECTION("No arg") {
        std::string x;
        formatTo(x, "123");
        formatTo(x, "Bla");
        CHECK(x == "123Bla");
    }
    SECTION("One arg") {
        std::string x;
        formatTo(x, "heuristic('{}')", "level");
        CHECK(x == "heuristic('level')");
    }
    SECTION("Multi arg") {
        std::string x;
        formatTo(x, "_edge({},{})", 29, 32);
        CHECK(x == "_edge(29,32)");
        x.clear();
        formatTo(x, "_heuristic({},{},{},{})", "foo", "level", -20, 30);
        CHECK(x == "_heuristic(foo,level,-20,30)");
    }
    SECTION("Extra args") {
        std::string x;
        formatTo(x, "Hello {}", "Foo", 22);
        CHECK(x == "Hello Foo");
    }
}

TEST_CASE("Macro test", "[macro]") {
    SECTION("test fail function") {
        REQUIRE_THROWS_AS(fail(Potassco::error_logic, nullptr, 0, nullptr, "Message with %d parameters {'%s', '%s'}", 2,
                               "Foo", "Bar"),
                          std::logic_error);
        REQUIRE_THROWS_WITH(fail(Potassco::error_logic, nullptr, 0, nullptr, "Message with %d parameters {'%s', '%s'}",
                                 2, "Foo", "Bar"),
                            "Message with 2 parameters {'Foo', 'Bar'}");

        REQUIRE_THROWS_AS(fail(Potassco::error_assert, nullptr, 0, "false", nullptr), std::logic_error);
        REQUIRE_THROWS_AS(fail(Potassco::error_runtime, nullptr, 0, "false", nullptr), std::runtime_error);
    }
    SECTION("calling fail with success is a logic error") {
        REQUIRE_THROWS_AS(fail(0, nullptr, 0, nullptr, nullptr), std::invalid_argument);
    }
    SECTION("test check") {
        REQUIRE_NOTHROW(POTASSCO_CHECK(true, EINVAL));
        REQUIRE_THROWS_AS(POTASSCO_CHECK(false, EINVAL), std::invalid_argument);
        REQUIRE_THROWS_AS(POTASSCO_CHECK(false, Potassco::error_assert), std::logic_error);
        REQUIRE_THROWS_AS(POTASSCO_CHECK(false, Potassco::error_logic), std::logic_error);
        REQUIRE_THROWS_AS(POTASSCO_CHECK(false, Potassco::error_runtime), std::runtime_error);
        REQUIRE_THROWS_AS(POTASSCO_CHECK(false, ENOMEM), std::bad_alloc);
    }
    SECTION("test check takes arguments") {
        REQUIRE_THROWS_WITH(POTASSCO_CHECK(false, EINVAL, "Shall fail %d", 123), Catch::Contains("Shall fail 123"));
    }
    SECTION("test require") {
        REQUIRE_NOTHROW(POTASSCO_REQUIRE(true));
        REQUIRE_NOTHROW(POTASSCO_REQUIRE(true, "with arg"));
        REQUIRE_THROWS_AS(POTASSCO_REQUIRE(false), std::logic_error);
        REQUIRE_THROWS_WITH(POTASSCO_REQUIRE(false, "Shall fail %d", 123), "Shall fail 123");
    }
    SECTION("test require without message contains expression") {
        REQUIRE_THROWS_WITH(POTASSCO_REQUIRE(false), Catch::Contains("check('false') failed"));
    }
    SECTION("test assert") {
        REQUIRE_NOTHROW(POTASSCO_ASSERT(true));
        REQUIRE_NOTHROW(POTASSCO_ASSERT(true, "with arg"));
        REQUIRE_THROWS_AS(POTASSCO_ASSERT(false), std::logic_error);
    }
    SECTION("test assert contains location") {
        // clang-format off
        REQUIRE_THROWS_WITH(POTASSCO_ASSERT(false), Catch::Contains(formatToStr("{}@{}: assertion failure: check('false')", POTASSCO_FUNC_NAME, __LINE__)));
        REQUIRE_THROWS_WITH(POTASSCO_ASSERT(false, "Shall fail %d", 123), Catch::Contains(formatToStr("{}@{}: assertion failure: Shall fail 123", POTASSCO_FUNC_NAME, __LINE__)));
        // clang-format on
    }
}

POTASSCO_ENUM(Foo_t, unsigned, Value1 = 0, Value2 = 1, Value3 = 2, Value4, Value5 = 7, Value6 = 7 + 1);

TEST_CASE("Enum test", "[enum]") {
    using namespace std::literals;
    static_assert(Potassco::enum_count<Foo_t>() == 6, "Wrong count");
    static_assert(Potassco::enum_name(Foo_t::Value3) == "Value3"sv, "Wrong name");

    SECTION("test entries") {
        using Potassco::enumDecl;
        using enum Foo_t;
        REQUIRE(Potassco::enum_entries<Foo_t>() ==
                std::array{enumDecl(Value1, "Value1"sv), enumDecl(Value2, "Value2"sv), enumDecl(Value3, "Value3"sv),
                           enumDecl(Value4, "Value4"sv), enumDecl(Value5, "Value5"sv), enumDecl(Value6, "Value6"sv)});
    }

    SECTION("test enum to string") {
        REQUIRE(toString(Foo_t::Value1) == "Value1");
        REQUIRE(toString(Foo_t::Value2) == "Value2");
        REQUIRE(toString(Foo_t::Value3) == "Value3");
        REQUIRE(toString(Foo_t::Value4) == "Value4");
        REQUIRE(toString(Foo_t::Value5) == "Value5");
        REQUIRE(toString(Foo_t::Value6) == "Value6");
        Foo_t unknown{12};
        REQUIRE(toString(unknown) == "12");
    }

    SECTION("test enum from string") {
        REQUIRE(string_cast<Foo_t>("Value3") == Foo_t::Value3);
        REQUIRE(string_cast<Foo_t>("7") == Foo_t::Value5);
        REQUIRE(string_cast<Foo_t>("Value4") == Foo_t::Value4);
        REQUIRE(string_cast<Foo_t>("8") == Foo_t::Value6);
        REQUIRE_THROWS_AS(string_cast<Foo_t>("9"), Potassco::bad_string_cast);
        REQUIRE_THROWS_AS(string_cast<Foo_t>("Value98"), Potassco::bad_string_cast);
        REQUIRE_THROWS_AS(string_cast<Foo_t>("Value"), Potassco::bad_string_cast);
    }
}

} // namespace Potassco::Test
