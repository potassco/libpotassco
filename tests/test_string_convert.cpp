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

#include <potassco/format.h>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <climits>
#include <string>
#include <vector>

namespace Potassco::Test {

template <typename T>
static auto string_cast(const std::string& in) -> std::optional<T> {
    if (T out{}; Potassco::Parse::ok(Potassco::stringTo(in, out))) {
        return out;
    }
    return std::nullopt;
}

TEST_CASE("String conversion", "[string]") {
#define REQUIRE_PARSE(EC, GOT, EXPECTED)                                                                               \
    REQUIRE(Parse::ok(EC));                                                                                            \
    REQUIRE((GOT) == (EXPECTED))

#define REQUIRE_PARSE_FAIL(EC, GOT, EXPECTED)                                                                          \
    REQUIRE_FALSE(Parse::ok(EC));                                                                                      \
    REQUIRE((GOT) == (EXPECTED))

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
    SECTION("works with string view") {
        const char*      source = "123";
        std::string_view in(source, 2);
        int              iVal{0};
        REQUIRE_PARSE(stringTo(in, iVal), iVal, 12);
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
        constexpr double d = 0.00000001;
        REQUIRE(string_cast<double>(Potassco::toString(d)) == d);

        float x{};
        REQUIRE(Potassco::Parse::ok(Potassco::stringTo("0.8", x)));
        REQUIRE(Potassco::toString(x) == "0.8");
    }

    SECTION("Pairs can be converted") {
        constexpr std::pair p(10, false);
        REQUIRE(Potassco::toString(p) == "10,false");
        REQUIRE(string_cast<std::pair<int, bool>>("10,false") == p);

        using IntPair = std::pair<int, int>;
        IntPair     x;
        std::string value("(1,2)");
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, IntPair(1, 2));
        REQUIRE_PARSE(Potassco::stringTo("7", x), x, IntPair(7, 2));

        REQUIRE_PARSE_FAIL(Potassco::stringTo("9,", x), x, IntPair(7, 2));
    }
    SECTION("Pairs can be nested") {
        using IntPair = std::pair<int, int>;
        std::pair<IntPair, IntPair> x;
        std::string                 value("((1,2),(3,4))");
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, std::pair(IntPair(1, 2), IntPair(3, 4)));
        value = "3,4,5,6";
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, std::pair(IntPair(3, 4), IntPair(5, 6)));
        value = "99";
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, std::pair(IntPair(99, 4), IntPair(5, 6)));
    }
    SECTION("Sequence can be converted") {
        REQUIRE(Potassco::toString(1, 2, 3) == "1,2,3");
        REQUIRE(Potassco::toString(1, "Hallo") == "1,Hallo");

        REQUIRE(Potassco::toString(std::vector{1, 2, 3}) == "1,2,3");
    }
    SECTION("conversion works with long long") {
        long long mx = LLONG_MAX, mn = LLONG_MIN, y = 0;
        REQUIRE_PARSE(Potassco::stringTo(Potassco::toString(mx), y), y, mx);
        REQUIRE_PARSE(Potassco::stringTo(Potassco::toString(mn), y), y, mn);
    }
    SECTION("conversion works with long long even if errno is initially set") {
        long long          mx = LLONG_MAX, y = 0;
        unsigned long long umx = ULLONG_MAX, z = 0;
        errno = ERANGE;
        REQUIRE_PARSE(Potassco::stringTo(Potassco::toString(mx), y), y, mx);

        auto s = Potassco::toString(ULLONG_MAX);
        errno  = ERANGE;
        REQUIRE_PARSE(Potassco::stringTo(s, z), z, umx);
    }

    SECTION("double parsing before local change") {
        double d  = 0;
        auto   in = "1233.22foo";
        auto   r  = fromChars(in, d);
        REQUIRE_PARSE(r, d, 1233.22);
        REQUIRE(r.ptr);
        CHECK(*r.ptr == 'f');
    }

    SECTION("double parsing is locale-independent") {
        auto [prevStr, prevLoc] = []() {
            using P          = std::pair<std::string, std::string>;
            std::string prev = setlocale(LC_ALL, nullptr);
            for (const auto& [language, territory] :
                 {P("deu", "deu"), P("de", "DE"), P("el", "GR"), P("ru", "RU"), P("es", "ES"), P("it", "IT")}) {
                for (auto sep : {'_', '-'}) {
                    for (const auto* codeset : {"", ".utf8"}) {
                        auto loc = std::string(language).append(1, sep).append(territory).append(codeset);
                        if (setlocale(LC_ALL, loc.c_str())) {
                            return std::make_pair(prev, std::locale::global(std::locale(loc)));
                        }
                    }
                }
            }
            return std::make_pair(std::string(), std::locale());
        }();
        if (not prevStr.empty()) {
            POTASSCO_SCOPE_EXIT({
                setlocale(LC_ALL, prevStr.c_str());
                std::locale::global(prevLoc);
            });
            REQUIRE(string_cast<double>("12.32") == 12.32);
            REQUIRE(string_cast<float>("12.32") == 12.32f);
        }
        else {
            WARN("could not set locale - test ignored");
        }
    }

    SECTION("double parsing stops at invalid pos") {
        double      d(0);
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
        REQUIRE_PARSE(r, d, expected);
        REQUIRE(r.ptr);
        CHECK(*r.ptr == next);
    }

    SECTION("double parsing supports zero and negative numbers") {
        double d = 2.0;
        REQUIRE_PARSE(fromChars("0", d), d, 0.0);
        d = 20.0;
        REQUIRE_PARSE(fromChars("0.000", d), d, 0.0);
        REQUIRE_PARSE(fromChars("-12.32", d), d, -12.32);
    }

    SECTION("vectors can be converted") {
        using Vec = std::vector<int>;
        Vec         x;
        std::string value("[1,2,3,4]");
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, Vec({1, 2, 3, 4}));
        REQUIRE_NOTHROW(x = string_cast<Vec>("1,2,3").value());
        REQUIRE(x == Vec({1, 2, 3}));
        REQUIRE(Potassco::stringTo("1,2,", x) != std::errc{});
    }
    SECTION("vectors can be nested") {
        using Vec    = std::vector<int>;
        using VecVec = std::vector<Vec>;
        VecVec      x;
        std::string value("[[1,2],[3,4]]");
        REQUIRE_PARSE(Potassco::stringTo(value, x), x, VecVec({{1, 2}, {3, 4}}));
    }

    SECTION("eqIgnoreCase") {
        REQUIRE(Potassco::Parse::eqIgnoreCase({}, {}));
        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase({}, "H"));
        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase("H", {}));
        REQUIRE(Potassco::Parse::eqIgnoreCase("H", "H"));
        REQUIRE(Potassco::Parse::eqIgnoreCase("h", "H"));
        REQUIRE(Potassco::Parse::eqIgnoreCase("haLlO", "HALLO"));
        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase("haLlO_", "HALLO"));
    }
    SECTION("eqIgnoreCase n") {
        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase({}, {}, 1));
        REQUIRE(Potassco::Parse::eqIgnoreCase({}, {}, 0));

        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase({}, "H", 1));
        REQUIRE(Potassco::Parse::eqIgnoreCase("H", "H", 1));
        REQUIRE_FALSE(Potassco::Parse::eqIgnoreCase("H", "H", 2));
        REQUIRE(Potassco::Parse::eqIgnoreCase("haL", "HALx", 3));
        REQUIRE(Potassco::Parse::eqIgnoreCase("haL", "HALx", 3));
    }

    SECTION("Num") {
        REQUIRE(toString(num(42)) == "42");
        REQUIRE(toString(num<4>(-42)) == " -42");
        REQUIRE(toString(num<-4>(42)) == "42  ");

        REQUIRE(toString(num<8>(4711u, 's')) == "   4711s");
        REQUIRE(toString(num<-3>(7u, '%')) == "7% ");

        REQUIRE(toString(num<0, 3>(0.12345)) == "0.123");
        REQUIRE(toString(num<6, 3>(0.12345)) == " 0.123");
        REQUIRE(toString(num<-7, 2>(0.12345)) == "0.12   ");
        REQUIRE(toString(num<-7, 2>(0.12345, 's')) == "0.12s  ");
    }
    SECTION("Str") {
        REQUIRE(toString(str("4711")) == "4711");
        REQUIRE(toString(str<-8>("4711")) == "4711    ");
        REQUIRE(toString(str<6>("4711")) == "  4711");
    }
    SECTION("Quoted") {
        REQUIRE(toString(quoted("Hallo")) == "\"Hallo\"");
        REQUIRE(toString(quoted("Hallo", "'")) == "'Hallo'");
        REQUIRE(toString(quoted(42, "'")) == "'42'");
    }
    SECTION("Keyed") {
        REQUIRE(toString(keyed("Hello", num<0, 3>(0.12345))) == "Hello: 0.123");
        REQUIRE(toString(keyed("Foo", quoted("Bar"))) == "Foo: \"Bar\"");
        REQUIRE(toString(keyed("", 23)) == "23");
    }
    SECTION("Styled") {
        REQUIRE(toString(styled("Hallo", TextStyle::Emphasis::bold)) == "\x1b[1mHallo\x1b[0m");
        REQUIRE(toString(styled(quoted("Hallo", "'"), TextStyle::Color::red | TextStyle::Emphasis::italic)) ==
                "\x1b[3;31m'Hallo'\x1b[0m");
    }
}
TEST_CASE("BasicCharBuffer", "[string]") {
    BasicCharBuffer buffer;
    REQUIRE(buffer.empty());
    REQUIRE(buffer.capacity() == 253);
    const auto* local = buffer.data();
    SECTION("move") {
        SECTION("small") {
            buffer.open(TextStyle::Color::red, '\n').append("Hello");
            BasicCharBuffer buffer2(std::move(buffer));
            REQUIRE(buffer.empty()); // NOLINT(bugprone-use-after-move)
            REQUIRE(buffer.data() == local);
            REQUIRE(buffer.close().empty());
            REQUIRE(buffer2.view() == "\x1b[0;31mHello");
            REQUIRE(buffer2.data() != local);
            REQUIRE(buffer2.close() == "\x1b[0;31mHello\x1b[0m\n");
            buffer = std::move(buffer2);
            REQUIRE(buffer2.empty()); // NOLINT(bugprone-use-after-move)
            REQUIRE(buffer2.close().empty());
            REQUIRE(buffer.view() == "\x1b[0;31mHello\x1b[0m\n");
            REQUIRE(buffer.data() == local);
            REQUIRE(buffer.close() == "\x1b[0;31mHello\x1b[0m\n");
        }
        SECTION("large") {
            std::string data(500, 'x');
            buffer.append(data);
            REQUIRE(buffer.data() != local);
            BasicCharBuffer buffer2(std::move(buffer));
            REQUIRE(buffer.empty()); // NOLINT(bugprone-use-after-move)
            REQUIRE(buffer.data() == local);
            REQUIRE(buffer.close().empty());
            REQUIRE(buffer2.view() == data);
            buffer = std::move(buffer2);
            REQUIRE(buffer2.empty()); // NOLINT(bugprone-use-after-move)
            REQUIRE(buffer2.close().empty());
            REQUIRE(buffer.view() == data);
            REQUIRE(buffer.data() != local);
        }
    }
    SECTION("copy") {
        SECTION("small") {
            buffer.open(TextStyle::Color::red, '\n').append("Hello");
            BasicCharBuffer buffer2(buffer);
            REQUIRE(buffer2.view() == "\x1b[0;31mHello");
            REQUIRE(buffer2.data() != local);
            REQUIRE(buffer2.close() == "\x1b[0;31mHello\x1b[0m\n");

            REQUIRE_FALSE(buffer.empty());
            REQUIRE(buffer.data() == local);
            REQUIRE(buffer.close() == "\x1b[0;31mHello\x1b[0m\n");
            REQUIRE(buffer.view() == buffer2.view());

            buffer.clear();
            REQUIRE(buffer.empty());
            buffer.append(buffer.capacity() + 1, 'x');
            REQUIRE(buffer.data() != local);

            auto nc = buffer.capacity();
            REQUIRE(nc > buffer2.capacity());
            buffer = buffer2;
            REQUIRE(buffer2.close() == "\x1b[0;31mHello\x1b[0m\n");

            REQUIRE(buffer.view() == "\x1b[0;31mHello\x1b[0m\n");
            REQUIRE(buffer.data() != local);
            REQUIRE(buffer.close() == "\x1b[0;31mHello\x1b[0m\n");
            REQUIRE(buffer.capacity() == nc);
        }
        SECTION("large") {
            std::string data(500, 'x');
            buffer.append(data);
            REQUIRE(buffer.data() != local);
            BasicCharBuffer buffer2(buffer);
            REQUIRE_FALSE(buffer.empty());
            REQUIRE(buffer.data() != local);
            REQUIRE(buffer.view() == data);
            REQUIRE(buffer2.view() == data);

            buffer2 = BasicCharBuffer();
            buffer2.open(TextStyle::Color::red, '\n').append("Hello");

            buffer = buffer2;
            REQUIRE_FALSE(buffer2.empty());
            REQUIRE(buffer2.close() == "\x1b[0;31mHello\x1b[0m\n");
            REQUIRE(buffer.data() != local);
            REQUIRE(buffer.view() == "\x1b[0;31mHello");
            REQUIRE(buffer.close() == "\x1b[0;31mHello\x1b[0m\n");
        }
    }
    SECTION("OpenClose") {
        REQUIRE(buffer.view().empty());
        buffer.open(TextStyle::Color::red, '\n');
        REQUIRE(buffer.view() == "\x1b[0;31m");
        buffer.append("Hello");
        REQUIRE(buffer.close() == "\x1b[0;31mHello\x1b[0m\n");
        buffer.clear();
        buffer.open(TextStyle::Color::green);
        buffer.append("World");
        REQUIRE(buffer.close() == "\x1b[0;32mWorld\x1b[0m");
        buffer.clear();
        buffer.open(TextStyle::Color::blue, 0);
        std::string exp("\x1b[0;34m\x1b[0m");
        exp.push_back(0);
        REQUIRE(buffer.close() == exp);
        buffer.clear();
        buffer.open(TextStyle(), ';');
        buffer.append("World");
        REQUIRE(buffer.view() == "World");
        REQUIRE(buffer.close() == "World;");
        buffer.clear();
        buffer.open(TextStyle::Color::red, ' ').append("Hello").open(TextStyle(), '!').append("World").close();
        REQUIRE(buffer.view() == "\x1b[0;31mHello\x1b[0m World!");
    }
    SECTION("appendSeq") {
        buffer.appendSep(" ", 1, 2.2, "Hallo", true);
        REQUIRE(buffer.view() == "1 2.2 Hallo true");
        buffer.clear();
        buffer.appendSep(";", 1, 2.2, "Hallo", true);
        REQUIRE(buffer.view() == "1;2.2;Hallo;true");

        SECTION("all empty") {
            buffer.clear();
            buffer.appendSep("<>", std::optional<int>{}, std::optional<int>{});
            REQUIRE(buffer.view().empty());
        }
        SECTION("empty followed by non empty") {
            buffer.clear();
            buffer.appendSep("<>", std::optional<int>{}, 1, 2);
            REQUIRE(buffer.view() == "1<>2");
        }
        SECTION("non empty followed by empty") {
            buffer.clear();
            buffer.appendSep("<>", 1, 2, std::optional<int>{});
            REQUIRE(buffer.view() == "1<>2");
        }
        SECTION("mixed") {
            buffer.clear();
            buffer.appendSep("<>", 1, std::optional<int>{}, 2);
            REQUIRE(buffer.view() == "1<>2");

            buffer.clear();
            buffer.appendSep("<>", std::optional<int>{}, 1, std::optional<int>{});
            REQUIRE(buffer.view() == "1");
            buffer.clear();
            buffer.appendSep("<>", std::optional<int>{}, 1, std::optional<int>{}, 2, std::optional<int>{});
            REQUIRE(buffer.view() == "1<>2");
        }
    }
    SECTION("appendField") {
        buffer.append(str<6>("4711"));
        REQUIRE(buffer.view() == "  4711");
        buffer.clear();
        buffer.append(num<-3>(7u, '%'));
        REQUIRE(buffer.view() == "7% ");
    }
    SECTION("appendChar") {
        buffer.append('(').append(4, 'x').push_back(')');
        REQUIRE(buffer.view() == "(xxxx)");
    }

    SECTION("appendF") {
        REQUIRE(BasicCharBuffer{}.appendF("Hello").view() == "Hello");
        REQUIRE(BasicCharBuffer{}.appendF("Hello %s", "World").c_str() == std::string_view{"Hello World"});
        REQUIRE(BasicCharBuffer{}.appendF("Hello %08u|%gs", 22, 3.1).view() == "Hello 00000022|3.1s");
        std::string exp("Hello ");
        exp.append(130, ' ');
        exp.append("foo");
        REQUIRE(BasicCharBuffer{}.appendF("Hello %130sfoo", "").c_str() == exp);
    }

    SECTION("appendFCornerCase") {
        BasicCharBuffer buf;
        std::string     exp;
        auto            small = GENERATE(false, true);
        CAPTURE(small);
        if (not small) {
            exp.append(buf.capacity() + 1, 'a');
            buf.append(buf.capacity() + 1, 'a');
        }
        else {
            REQUIRE(buf.capacity() > 2);
        }
        auto avail = buf.capacity() - buf.size();
        buf.append(avail - 2, 'x');
        exp.append(avail - 2, 'x');
        REQUIRE(buf.view() == exp);
        auto* d = buf.data();
        exp.append("12");
        SECTION("full-cap") {
            REQUIRE(buf.appendF("%d", 12).view() == exp);
            REQUIRE(d == buf.data());
            REQUIRE(buf.data()[exp.size()] == 0);
        }
        SECTION("grow") {
            auto oldCap  = buf.capacity();
            exp         += "3";
            REQUIRE(buf.appendF("%d", 123).view() == exp);
            REQUIRE(buf.capacity() > oldCap);
            REQUIRE(buf.data()[exp.size()] == 0);
        }
    }
    SECTION("smallToLarge") {
        BasicCharBuffer buf;
        auto*           d = buf.data();
        std::string     s(buf.capacity(), 'x');
        buf.append(s);
        REQUIRE(buf.size() == buf.capacity());
        REQUIRE(buf.data() == d);
        REQUIRE(buf.size() == s.size());
        REQUIRE(buf.view() == s);
        REQUIRE(buf.c_str() == s);
        REQUIRE(d[s.size()] == 0);
        buf.push_back('x');
        s.push_back('x');
        REQUIRE(buf.size() == s.size());
        REQUIRE(buf.capacity() > buf.size());
        REQUIRE(buf.data() != d);
        REQUIRE(buf.back() == 'x');
        REQUIRE(buf.view() == s);
        REQUIRE(buf.data()[buf.size()] == 0);

        buf.append(buf.capacity() - buf.size(), 'y');
        s.resize(buf.capacity(), 'y');
        REQUIRE(buf.view() == s);
        REQUIRE(buf.size() == buf.capacity());
        REQUIRE(buf.data()[buf.size()] == 0);
    }
}
namespace {
enum class Foo : unsigned { value1 = 0, value2 = 1, value3 = 2, value4, value5 = 7, value6 = 7 + 1 };
enum class Char : char { value1 = 0, value2 = 1, value3 = 2, value4, value5 = 7, value6 = 7 + 1 };
enum class Byte : char { value1 = 0, value2 = 1, value3 = 2, value4, value5 = 7, value6 = 7 + 1 };
POTASSCO_REFLECT_ENUM_ENTRIES(Foo, 0u, 8u);
POTASSCO_REFLECT_ENUM_ENTRIES(Char, 0u, 8u);
POTASSCO_REFLECT_ENUM_ENTRIES(Byte, 0u, 8u);
} // namespace
using namespace std::literals;
static_assert(Potassco::enum_count<Foo>() == 6, "Wrong count");
static_assert(Potassco::enum_name(Foo::value3) == "value3"sv, "Wrong name");
TEST_CASE("Enum entries", "[enum]") {
    using P = std::pair<Foo, std::string_view>;
    using A = std::array<P, 6>;
    using enum Foo;

    auto     expected = std::array{P(value1, "value1"sv), P(value2, "value2"sv), P(value3, "value3"sv),
                               P(value4, "value4"sv), P(value5, "value5"sv), P(value6, "value6"sv)};
    const A& got      = Potassco::enum_entries<Foo>();
    REQUIRE(got == expected);

    REQUIRE(Potassco::enum_min<Foo>() == 0);
    REQUIRE(Potassco::enum_max<Foo>() == 8);
    REQUIRE_FALSE(Potassco::enum_cast<Foo>(4).has_value());
    REQUIRE_FALSE(Potassco::enum_cast<Foo>(5).has_value());
    REQUIRE_FALSE(Potassco::enum_cast<Foo>(6).has_value());
    REQUIRE(Potassco::enum_cast<Foo>(7) == Foo::value5);
    enum NoMeta : uint8_t {};
    REQUIRE(Potassco::enum_min<NoMeta>() == 0u);
    REQUIRE(Potassco::enum_max<NoMeta>() == 255u);
}

TEMPLATE_TEST_CASE("Enum to string", "[enum]", Foo, Char, Byte) {
    using E = TestType;
    REQUIRE(toString(E::value1) == "value1");
    REQUIRE(toString(E::value2) == "value2");
    REQUIRE(toString(E::value3) == "value3");
    REQUIRE(toString(E::value4) == "value4");
    REQUIRE(toString(E::value5) == "value5");
    REQUIRE(toString(E::value6) == "value6");
    REQUIRE(toString(std::vector{E::value1, E::value3, E::value6}) == "value1,value3,value6");
    E unknown{12};
    REQUIRE(toString(unknown) == "12");
}

TEMPLATE_TEST_CASE("Enum from string", "[enum]", Foo, Char, Byte) {
    using E = TestType;
    REQUIRE(string_cast<E>("Value3") == E::value3);
    REQUIRE(string_cast<E>("7") == E::value5);
    REQUIRE(string_cast<E>("Value4") == E::value4);
    REQUIRE(string_cast<E>("vAlUe4") == E::value4);
    REQUIRE(string_cast<E>("8") == E::value6);
    REQUIRE_FALSE(string_cast<E>("9").has_value());
    REQUIRE_FALSE(string_cast<E>("Value98").has_value());
    REQUIRE_FALSE(string_cast<E>("Value").has_value());
}

} // namespace Potassco::Test
