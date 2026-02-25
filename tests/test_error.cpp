//
// Copyright (c) 2024 - present, Benjamin Kaufmann
//
// This file is part of Potassco.
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
//

#include <potassco/error.h>

#include <potassco/clingo.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

namespace User {
struct Error_t {};
struct Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] void failThrow(Error_t, const Potassco::ExpressionInfo& info, std::string m) {
    m.append(" with failed expression: ").append(info.expression);
    throw Error(m);
}
} // namespace User

namespace Potassco::Test {
static auto messageContains(const std::string& s) {
    return Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring(s));
}
static auto messageEquals(const std::string& s) { return Catch::Matchers::MessageMatches(Catch::Matchers::Equals(s)); }
static auto messageFmtEq(std::string_view fmt, const auto&... args) {
    auto m = std::string{fmt};
    auto r = std::initializer_list<std::string_view>{args...};
    auto a = r.begin();
    for (std::size_t p = 0;; p += a->length(), ++a) {
        if (p = m.find("{}", p); p == std::string::npos) {
            break;
        }
        REQUIRE(a != r.end());
        m.replace(p, 2, *a);
    }
    REQUIRE(a == r.end());
    return messageEquals(m);
}
POTASSCO_WARNING_PUSH()
POTASSCO_WARNING_IGNORE_MSVC(4702) // unreachable code
TEST_CASE("Assertion and Error", "[error]") {
    auto makeError    = [](std::errc ec = std::errc::invalid_argument) { return Detail::translateEc(ec); };
    auto makeLocation = [](const std::source_location& loc, bool includeFile, const char* m = "") -> std::string {
        std::ostringstream os;
        if (not includeFile) {
            os << loc.function_name() << ':' << loc.line();
        }
        else {
            os << ExpressionInfo::relativeFileName(loc) << ':' << loc.line() << ": " << loc.function_name();
        }
        os << ':' << (m && *m ? " " : "") << m;
        return std::move(os).str();
    };

    SECTION("fail type") {
        auto e = POTASSCO_CAPTURE_EXPRESSION(expression);
        SECTION("recoverable error") {
            auto loc        = makeLocation(e.location, false);
            auto defMessage = std::make_error_code(std::errc::invalid_argument).message();

            SECTION("no message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e), std::invalid_argument,
                                     messageFmtEq("{}\n{} check '{}' failed.", defMessage, loc, "expression"));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e), std::invalid_argument,
                                     messageFmtEq("{}\n{} failed.", defMessage, loc));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(
                    Potassco::failThrow(makeError(), e, "custom message"), std::invalid_argument,
                    messageFmtEq("{}: {}\n{} check '{}' failed.", "custom message", defMessage, loc, e.expression));

                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e, "custom message with args %u %s", 1, "bla"),
                                     std::invalid_argument,
                                     messageFmtEq("{}: {}\n{} check '{}' failed.", "custom message with args 1 bla",
                                                  defMessage, loc, e.expression));
            }
        }

        SECTION("precondition") {
            auto loc = makeLocation(e.location, false);
            SECTION("no message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(Errc::precondition_fail, e), std::invalid_argument,
                                     messageFmtEq("{} Precondition '{}' failed.", loc, e.expression));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failThrow(Errc::precondition_fail, e), std::invalid_argument,
                                     messageFmtEq("{} Precondition failed.", loc));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(
                    Potassco::failThrow(Errc::precondition_fail, e, "custom message"), std::invalid_argument,
                    messageFmtEq("{} Precondition '{}' failed.\nmessage: {}", loc, e.expression, "custom message"));

                CHECK_THROWS_MATCHES(
                    Potassco::failThrow(Errc::precondition_fail, e, "custom message with args %u %s", 1, "bla"),
                    std::invalid_argument,
                    messageFmtEq("{} Precondition '{}' failed.\nmessage: {}", loc, e.expression,
                                 "custom message with args 1 bla"));
            }
        }

        SECTION("assert") {
            // NOTE: Since Catch2 does not support "death tests", we cannot test the default assertion behavior,
            // which would simply abort the process.
            auto old = Potassco::setAbortHandler(+[](const char* msg) { throw std::logic_error(msg); });
            POTASSCO_SCOPE_EXIT({ Potassco::setAbortHandler(old); });
            auto loc = makeLocation(e.location, true);
            SECTION("no message") {
                CHECK_THROWS_MATCHES(Potassco::failAbort(e), std::logic_error,
                                     messageFmtEq("{} Assertion '{}' failed.", loc, e.expression));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failAbort(e), std::logic_error,
                                     messageFmtEq("{} Assertion failed.", loc));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(
                    Potassco::failAbort(e, "custom message"), std::logic_error,
                    messageFmtEq("{} Assertion '{}' failed.\nmessage: {}", loc, e.expression, "custom message"));

                CHECK_THROWS_MATCHES(Potassco::failAbort(e, "custom message with args %u %s", 1, "bla"),
                                     std::logic_error,
                                     messageFmtEq("{} Assertion '{}' failed.\nmessage: {}", loc, e.expression,
                                                  "custom message with args 1 bla"));
            }
        }
    }

    SECTION("fail ec") {
        auto           e         = POTASSCO_CAPTURE_EXPRESSION(expression);
        constexpr auto errcMatch = [](Errc lhs, std::errc rhs) { return to_underlying(lhs) == to_underlying(rhs); };
        SECTION("logic errors") {
            static_assert(errcMatch(Errc::invalid_argument, std::errc::invalid_argument), "unexpected mapping");
            static_assert(errcMatch(Errc::domain_error, std::errc::argument_out_of_domain), "unexpected mapping");
            static_assert(errcMatch(Errc::out_of_range, std::errc::result_out_of_range), "unexpected mapping");
            static_assert(errcMatch(Errc::length_error, std::errc::argument_list_too_long), "unexpected mapping");

            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::invalid_argument), e, "my message"),
                              std::invalid_argument);
            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::argument_out_of_domain), e, "my message"),
                              std::domain_error);
            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::result_out_of_range), e, "my message"), std::out_of_range);
            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::argument_list_too_long), e, "my message"),
                              std::length_error);
        }

        SECTION("out of memory") {
            static_assert(errcMatch(Errc::bad_alloc, std::errc::not_enough_memory), "unexpected mapping");
            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::not_enough_memory), e, "my message"), std::bad_alloc);
            REQUIRE_THROWS_AS(failThrow(Errc::bad_alloc, e, "my message"), std::bad_alloc);
        }

        SECTION("runtime error") {
            static_assert(errcMatch(Errc::overflow_error, std::errc::value_too_large), "unexpected mapping");
            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::bad_file_descriptor), e, "my message"), RuntimeError);
            REQUIRE_THROWS_AS(failThrow(Detail::translateEc(EINTR), e, "my message"), RuntimeError);

            REQUIRE_THROWS_AS(failThrow(makeError(std::errc::value_too_large), e, "my message"), std::overflow_error);
        }
    }

    SECTION("Macros") {
        SECTION("fail") {
            CHECK_THROWS_AS(POTASSCO_FAIL(std::errc::not_enough_memory), std::bad_alloc);
            CHECK_THROWS_MATCHES(POTASSCO_FAIL(std::errc::invalid_argument, "not good enough"), std::invalid_argument,
                                 messageContains("not good enough"));
            CHECK_THROWS_MATCHES(POTASSCO_FAIL(Errc::length_error, "at most %d allowed", 3), std::length_error,
                                 messageContains("at most 3 allowed"));
        }

        SECTION("check") {
            CHECK_NOTHROW(POTASSCO_CHECK(true, std::errc::invalid_argument));
            CHECK_NOTHROW(POTASSCO_CHECK(true, std::errc::invalid_argument, "foo"));
            CHECK_NOTHROW(POTASSCO_CHECK(true, std::errc::invalid_argument, "%s", "foo"));

            CHECK_THROWS_MATCHES(POTASSCO_CHECK(false, std::errc::argument_out_of_domain), std::domain_error,
                                 messageContains("check 'false' failed"));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK(false, std::errc::argument_out_of_domain, "Message"), std::domain_error,
                                 messageContains("Message"));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK(false, std::errc::illegal_byte_sequence, "Message %d", 2), RuntimeError,
                                 messageContains("Message 2"));
            CHECK_THROWS_AS(POTASSCO_CHECK(false, std::errc::not_enough_memory, "Message %d", 2), std::bad_alloc);

            CHECK_THROWS_MATCHES(
                POTASSCO_CHECK(false, EAGAIN), RuntimeError,
                messageContains(std::make_error_code(std::errc::resource_unavailable_try_again).message()));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK(1 != 1, -EINVAL), std::invalid_argument,
                                 messageContains("check '1 != 1' failed"));

            CHECK_THROWS_MATCHES(POTASSCO_CHECK(1 != 1, User::Error_t{}, "found via adl"), User::Error,
                                 messageEquals("found via adl with failed expression: 1 != 1"));
        }

        SECTION("precondition") {
            CHECK_NOTHROW(POTASSCO_CHECK_PRE(true));
            CHECK_NOTHROW(POTASSCO_CHECK_PRE(true, "custom message"));
            CHECK_NOTHROW(POTASSCO_CHECK_PRE(true, "%s", "custom message"));
            CHECK_NOTHROW(POTASSCO_DEBUG_CHECK_PRE(true));

            CHECK_THROWS_MATCHES(POTASSCO_CHECK_PRE(false), std::invalid_argument,
                                 messageContains("Precondition 'false' failed"));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK_PRE(false), std::invalid_argument, messageContains(POTASSCO_FUNC_NAME));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK_PRE(false, "custom message"), std::invalid_argument,
                                 messageContains("custom message"));
            CHECK_THROWS_MATCHES(POTASSCO_CHECK_PRE(false, "%s %d", "foo", 2), std::invalid_argument,
                                 messageContains("foo 2"));
        }

        SECTION("assert") {
            CHECK_NOTHROW(POTASSCO_ASSERT(true));
            CHECK_NOTHROW(POTASSCO_ASSERT(true, "custom message"));
            CHECK_NOTHROW(POTASSCO_ASSERT(true, "%s", "custom message"));
            CHECK_NOTHROW(POTASSCO_DEBUG_ASSERT(true));

            auto old = Potassco::setAbortHandler(+[](const char* msg) { throw std::logic_error(msg); });
            POTASSCO_SCOPE_EXIT({ Potassco::setAbortHandler(old); });
            using sc = std::source_location;
            // clang-format off
            CHECK_THROWS_WITH(POTASSCO_ASSERT(false), makeLocation(sc::current(), true, "Assertion 'false' failed."));
            CHECK_THROWS_WITH(POTASSCO_ASSERT(false, "Fail %d", 123), makeLocation(sc::current(), true, "Assertion 'false' failed.\nmessage: Fail 123"));
            CHECK_THROWS_WITH(POTASSCO_ASSERT_NOT_REACHED("foo"), makeLocation(sc::current(), true, "Assertion 'not reached' failed.\nmessage: foo"));
            // clang-format on
        }
    }
}
POTASSCO_WARNING_POP()

TEST_CASE("Scope exit", "[error]") {
    SECTION("simple") {
        bool called = false;
        {
            POTASSCO_SCOPE_EXIT({ called = true; });
            CHECK(called == false);
        }
        CHECK(called == true);
    }
    SECTION("exception") {
        bool called = false;
        try {
            POTASSCO_SCOPE_EXIT({ called = true; });
            throw std::runtime_error("foo");
        }
        catch (const std::exception&) {
            CHECK(called == true);
        }
    }
    SECTION("is allowed to throw") {
        try {
            {
                POTASSCO_SCOPE_EXIT({ throw std::runtime_error("foo"); });
            }
            FAIL();
        }
        catch (const std::runtime_error&) {
        }
    }
    SECTION("can be nested") {
        std::string s;
        {
            POTASSCO_SCOPE_EXIT({
                s += "1";
                POTASSCO_SCOPE_EXIT({ s += "nest"; });
                s += "1";
            });
            POTASSCO_SCOPE_EXIT({ s += "2"; });
        }
        CHECK(s == "211nest");
    }
}
TEST_CASE("Statistics", "[error]") {
    SECTION("type error") {
        STATIC_CHECK(enum_name(StatisticsType::map) == "map");
        STATIC_CHECK(enum_name(StatisticsType::array) == "array");
        STATIC_CHECK(enum_name(StatisticsType::value) == "value");
        auto typeError = [](StatisticsType e, StatisticsType g) {
            return messageFmtEq("bad stats access: '{}' expected but got '{}'", enum_name(e), enum_name(g));
        };
        CHECK_THROWS_MATCHES(AbstractStatistics::throwType(StatisticsType::map, StatisticsType::value),
                             std::logic_error, typeError(StatisticsType::map, StatisticsType::value));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwType(StatisticsType::value, StatisticsType::array),
                             std::logic_error, typeError(StatisticsType::value, StatisticsType::array));
    }
    SECTION("key error") {
        auto keyError = [](AbstractStatistics::Key_t k) {
            return messageFmtEq("bad stats access: invalid key '{}'", std::to_string(k));
        };
        CHECK_THROWS_MATCHES(AbstractStatistics::throwKey(123), std::logic_error, keyError(123));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwKey(0xDEADBEEF), std::logic_error, keyError(0xDEADBEEF));
    }
    SECTION("path error") {
        auto pathError = [](const auto&... args) {
            std::string t("bad stats access: invalid key '{}'");
            t.append(sizeof...(args) == 2 ? " in path '{}'" : "");
            return messageFmtEq(t, args...);
        };
        CHECK_THROWS_MATCHES(AbstractStatistics::throwPath("foo.bar.bla", "bla"), std::out_of_range,
                             pathError("bla", "foo.bar.bla"));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwPath("", "bla"), std::out_of_range, pathError("bla"));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwPath("foo.bar.bla", ""), std::out_of_range,
                             pathError("foo.bar.bla"));
    }
    SECTION("write error") {
        auto writeError = [](AbstractStatistics::Key_t k, StatisticsType t) {
            return messageFmtEq("bad stats access: key '{}' is not a writable {}", std::to_string(k), enum_name(t));
        };
        CHECK_THROWS_MATCHES(AbstractStatistics::throwWrite(123, StatisticsType::map), std::logic_error,
                             writeError(123, StatisticsType::map));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwWrite(102040, StatisticsType::array), std::logic_error,
                             writeError(102040, StatisticsType::array));
        CHECK_THROWS_MATCHES(AbstractStatistics::throwWrite(0xDEADBEEF, StatisticsType::value), std::logic_error,
                             writeError(0xDEADBEEF, StatisticsType::value));
    }
    SECTION("range error") {
        auto rangeError = [](std::size_t idx, std::size_t size) {
            return messageFmtEq("bad stats access: index '{}' is out of range for object of size '{}'",
                                std::to_string(idx), std::to_string(size));
        };
        CHECK_THROWS_MATCHES(AbstractStatistics::throwRange(123, 120), std::out_of_range, rangeError(123, 120));
    }
}

} // namespace Potassco::Test
