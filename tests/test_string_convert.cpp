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
#include <potassco/program_opts/string_convert.h>

#include <potassco/error.h>

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <string>
#include <vector>

namespace Potassco::Test {

template <typename T>
static std::optional<T> string_cast(const std::string& in) {
    T out;
    if (auto ec = Potassco::stringTo(in, out); ec != std::errc{})
        return std::nullopt;
    return out;
}

TEST_CASE("String conversion", "[string]") {
    errno = 0;
    SECTION("empty string is not an int") {
        int      iVal;
        unsigned uVal;
        REQUIRE(stringTo("", iVal) == std::errc::invalid_argument);
        REQUIRE(stringTo("", uVal) == std::errc::invalid_argument);
        REQUIRE(Potassco::fromChars("", iVal).ec == std::errc::invalid_argument);
        REQUIRE(Potassco::fromChars("", uVal).ec == std::errc::invalid_argument);
    }
    SECTION("at least one digit") {
        int      iVal;
        unsigned uVal;
        REQUIRE(stringTo("+", iVal) == std::errc::invalid_argument);
        REQUIRE(stringTo("+", uVal) == std::errc::invalid_argument);
        REQUIRE(Potassco::fromChars("+", iVal).ec == std::errc::invalid_argument);
        REQUIRE(Potassco::fromChars("+", uVal).ec == std::errc::invalid_argument);
    }
    SECTION("overflow is an error") {
        int64_t  iVal;
        uint64_t uVal;

        REQUIRE(stringTo("18446744073709551616", iVal) == std::errc::result_out_of_range);
        REQUIRE(stringTo("18446744073709551616", uVal) == std::errc::result_out_of_range);

        REQUIRE(Potassco::fromChars("18446744073709551616", iVal).ec == std::errc::result_out_of_range);
        REQUIRE(Potassco::fromChars("18446744073709551616", uVal).ec == std::errc::result_out_of_range);
    }
    SECTION("positive and negative ints convert to string") {
        REQUIRE(Potassco::toString(10) == "10");
        REQUIRE(Potassco::toString(-10) == "-10");
    }
    SECTION("unsigned -1 converts to named limit") {
        REQUIRE(Potassco::toString(static_cast<unsigned int>(-1)) == "umax");
        REQUIRE(Potassco::toString(static_cast<unsigned long>(-1)) == "umax");
        REQUIRE(string_cast<unsigned int>("umax") == static_cast<unsigned int>(-1));
        REQUIRE(string_cast<unsigned long>("umax") == static_cast<unsigned long>(-1));
        REQUIRE(string_cast<unsigned long long>("umax") == static_cast<unsigned long long>(-1));
        REQUIRE(string_cast<uint64_t>("umax") == static_cast<uint64_t>(-1));
    }
    SECTION("-1 is only signed value accepted as unsigned") {
        REQUIRE(string_cast<unsigned int>("-1") == static_cast<unsigned int>(-1));
        unsigned long long out;
        REQUIRE(stringTo("-2", out) == std::errc::invalid_argument);
    }
    SECTION("umax does not apply to signed int") {
        REQUIRE_FALSE(string_cast<int>("umax").has_value());
        REQUIRE_FALSE(string_cast<long>("umax").has_value());
        REQUIRE_FALSE(string_cast<long long>("umax").has_value());
        REQUIRE_FALSE(string_cast<int64_t>("umax").has_value());
    }
    SECTION("named limits convert to signed ints") {
        REQUIRE(string_cast<int>("imax") == INT_MAX);
        REQUIRE(string_cast<int>("imin") == INT_MIN);

        REQUIRE(string_cast<long>("imax") == LONG_MAX);
        REQUIRE(string_cast<long>("imin") == LONG_MIN);

        REQUIRE(string_cast<long long>("imax") == LLONG_MAX);
        REQUIRE(string_cast<long long>("imin") == LLONG_MIN);
    }

    SECTION("chars are handled") {
        REQUIRE(string_cast<char>("\t") == '\t');
        REQUIRE(string_cast<char>("\r") == '\r');
        REQUIRE(string_cast<char>("\n") == '\n');
        REQUIRE(string_cast<char>("\v") == '\v');
        REQUIRE(string_cast<char>("\f") == '\f');
        REQUIRE(string_cast<char>("\a") == '\a');

        REQUIRE(string_cast<char>("x") == 'x');
        REQUIRE(string_cast<char>("a") == 'a');
        REQUIRE(string_cast<char>("H") == 'H');

        REQUIRE(string_cast<char>("49") == 49);
        REQUIRE_FALSE(string_cast<char>("256").has_value());
    }

    SECTION("char accepts escaped space") {
        REQUIRE(string_cast<char>("\\t") == '\t');
        REQUIRE(string_cast<char>("\\r") == '\r');
        REQUIRE(string_cast<char>("\\n") == '\n');
        REQUIRE(string_cast<char>("\\v") == '\v');
        REQUIRE(string_cast<char>("\\f") == '\f');

        REQUIRE_FALSE(string_cast<char>("\\a").has_value());
    }

    SECTION("bools are handled") {
        REQUIRE(string_cast<bool>("1").value());
        REQUIRE(string_cast<bool>("true").value());
        REQUIRE(string_cast<bool>("on").value());
        REQUIRE(string_cast<bool>("yes").value());

        REQUIRE_FALSE(string_cast<bool>("0").value());
        REQUIRE_FALSE(string_cast<bool>("false").value());
        REQUIRE_FALSE(string_cast<bool>("off").value());
        REQUIRE_FALSE(string_cast<bool>("no").value());

        REQUIRE(Potassco::toString(true) == "true");
        REQUIRE(Potassco::toString(false) == "false");
    }

    SECTION("double converts to string") { REQUIRE(Potassco::toString(10.2) == "10.2"); }
    SECTION("double conversion is reversible") {
        const double d = 0.00000001;
        REQUIRE(string_cast<double>(Potassco::toString(d)) == d);
    }
    SECTION("Pairs can be converted") {
        const std::pair<int, bool> p(10, false);
        REQUIRE(Potassco::toString(p) == "10,false");
        REQUIRE((string_cast<std::pair<int, bool>>("10,false") == p));

        using IntPair = std::pair<int, int>;
        IntPair     x;
        std::string value("(1,2)");
        bool        ok = Potassco::stringTo(value, x) == std::errc{};
        REQUIRE(ok);
        REQUIRE(x == IntPair(1, 2));
        REQUIRE(Potassco::stringTo("7", x) == std::errc{});
        REQUIRE(x == IntPair(7, 2));

        REQUIRE(Potassco::stringTo("9,", x) != std::errc{});
        REQUIRE(x == IntPair(7, 2));
    }
    SECTION("Pairs can be nested") {
        using IntPair = std::pair<int, int>;
        std::pair<IntPair, IntPair> x;
        std::string                 value("((1,2),(3,4))");
        REQUIRE(Potassco::stringTo(value, x) == std::errc{});
        REQUIRE((x.first == IntPair(1, 2) && x.second == IntPair(3, 4)));
        value = "3,4,5,6";
        REQUIRE(Potassco::stringTo(value, x) == std::errc{});
        REQUIRE((x.first == IntPair(3, 4) && x.second == IntPair(5, 6)));
        value = "99";
        REQUIRE(Potassco::stringTo(value, x) == std::errc{});
        REQUIRE((x.first == IntPair(99, 4) && x.second == IntPair(5, 6)));
    }
    SECTION("Sequence can be converted") {
        REQUIRE(Potassco::toString(1, 2, 3) == "1,2,3");
        REQUIRE(Potassco::toString(1, "Hallo") == "1,Hallo");

        REQUIRE(Potassco::toString(std::vector{1, 2, 3}) == "1,2,3");
    }
    SECTION("conversion works with long long") {
        long long mx = LLONG_MAX, mn = LLONG_MIN, y;
        REQUIRE((Potassco::stringTo(Potassco::toString(mx).c_str(), y) == std::errc{} && mx == y));
        REQUIRE((Potassco::stringTo(Potassco::toString(mn).c_str(), y) == std::errc{} && mn == y));
    }
    SECTION("conversion works with long long even if errno is initially set") {
        long long          mx  = LLONG_MAX, y;
        unsigned long long umx = ULLONG_MAX, z;
        errno                  = ERANGE;
        REQUIRE((Potassco::stringTo(Potassco::toString(mx).c_str(), y) == std::errc{} && mx == y));

        auto s = Potassco::toString(ULLONG_MAX);
        errno  = ERANGE;
        REQUIRE((Potassco::stringTo(s.c_str(), z) == std::errc{} && umx == z));
    }

    SECTION("double parsing before local change") {
        double d;
        auto   in = "1233.22foo";
        auto   r  = fromChars(in, d);
        CHECK(d == 1233.22);
        CHECK(r.ec == std::errc{});
        CHECK((r.ptr && *r.ptr == 'f'));
    }

    SECTION("double parsing is locale-independent") {
        std::string restore = []() {
            using P          = std::pair<std::string, std::string>;
            std::string prev = setlocale(LC_ALL, nullptr);
            if (setlocale(LC_ALL, "deu_deu"))
                return prev;
            for (const auto& [language, territory] :
                 {P("de", "DE"), P("el", "GR"), P("ru", "RU"), P("es", "ES"), P("it", "IT")}) {
                for (auto sep : {'_', '-'}) {
                    for (const auto* codeset : {"", ".utf8"}) {
                        auto loc = std::string(language).append(1, sep).append(territory).append(codeset);
                        if (setlocale(LC_ALL, loc.c_str())) {
                            return prev;
                        }
                    }
                }
            }
            return std::string{};
        }();
        if (not restore.empty()) {
            POTASSCO_SCOPE_EXIT({ setlocale(LC_ALL, restore.c_str()); });
            REQUIRE(string_cast<double>("12.32") == 12.32);
        }
        else {
            WARN("could not set locale - test ignored");
        }
    }

    SECTION("double parsing stops at invalid pos") {
        double      d;
        std::string what;
        char        next(0);
        double      expected = 1233.22;
        SECTION("sep") {
            what = "1233.22,foo";
            next = ',';
        }
        SECTION("corner case libc++") {
            what = "1233.22foo";
            next = 'f';
        }
        SECTION("corner case") {
            what     = "1Eblub";
            next     = 'E';
            expected = 1;
        }
        INFO(what);
        auto r = fromChars(what, d);
        CHECK(d == expected);
        CHECK(r.ec == std::errc{});
        CHECK((r.ptr && *r.ptr == next));
    }

    SECTION("vectors can be converted") {
        using Vec = std::vector<int>;
        Vec         x;
        std::string value("[1,2,3,4]");
        REQUIRE(Potassco::stringTo(value, x) == std::errc{});
        REQUIRE(x.size() == 4);
        REQUIRE(x[0] == 1);
        REQUIRE(x[1] == 2);
        REQUIRE(x[2] == 3);
        REQUIRE(x[3] == 4);
        REQUIRE_NOTHROW(x = string_cast<Vec>("1,2,3").value());
        REQUIRE(x.size() == 3);
        REQUIRE(Potassco::stringTo("1,2,", x) != std::errc{});
    }
    SECTION("vectors can be nested") {
        using Vec    = std::vector<int>;
        using VecVec = std::vector<Vec>;
        VecVec      x;
        std::string value("[[1,2],[3,4]]");
        REQUIRE(Potassco::stringTo(value, x) == std::errc{});
        REQUIRE((x.size() == 2 && x[0].size() == 2 && x[1].size() == 2));
        REQUIRE(x[0][0] == 1);
        REQUIRE(x[0][1] == 2);
        REQUIRE(x[1][0] == 3);
        REQUIRE(x[1][1] == 4);
    }
}

POTASSCO_ENUM(Foo_t, unsigned, Value1 = 0, Value2 = 1, Value3 = 2, Value4, Value5 = 7, Value6 = 7 + 1);
using namespace std::literals;
static_assert(Potassco::enum_count<Foo_t>() == 6, "Wrong count");
static_assert(Potassco::enum_name(Foo_t::Value3) == "Value3"sv, "Wrong name");

TEST_CASE("Enum entries", "[enum]") {
    using Potassco::enumDecl;
    using enum Foo_t;
    REQUIRE(Potassco::enum_entries<Foo_t>() == std::array{enumDecl(Value1, "Value1"sv), enumDecl(Value2, "Value2"sv),
                                                          enumDecl(Value3, "Value3"sv), enumDecl(Value4, "Value4"sv),
                                                          enumDecl(Value5, "Value5"sv), enumDecl(Value6, "Value6"sv)});
}

TEST_CASE("Enum to string", "[enum]") {
    REQUIRE(toString(Foo_t::Value1) == "Value1");
    REQUIRE(toString(Foo_t::Value2) == "Value2");
    REQUIRE(toString(Foo_t::Value3) == "Value3");
    REQUIRE(toString(Foo_t::Value4) == "Value4");
    REQUIRE(toString(Foo_t::Value5) == "Value5");
    REQUIRE(toString(Foo_t::Value6) == "Value6");
    Foo_t unknown{12};
    REQUIRE(toString(unknown) == "12");
}

TEST_CASE("Enum from string", "[enum]") {
    REQUIRE(string_cast<Foo_t>("Value3") == Foo_t::Value3);
    REQUIRE(string_cast<Foo_t>("7") == Foo_t::Value5);
    REQUIRE(string_cast<Foo_t>("Value4") == Foo_t::Value4);
    REQUIRE(string_cast<Foo_t>("8") == Foo_t::Value6);
    REQUIRE_FALSE(string_cast<Foo_t>("9").has_value());
    REQUIRE_FALSE(string_cast<Foo_t>("Value98").has_value());
    REQUIRE_FALSE(string_cast<Foo_t>("Value").has_value());
}

} // namespace Potassco::Test
