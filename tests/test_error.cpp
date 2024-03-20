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
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

namespace User {
struct Error_t {};
struct Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
void failThrow(Error_t, const Potassco::ExpressionInfo& info, std::string m) {
    m.append(" with failed expression: ").append(info.expression);
    throw Error(m);
}
} // namespace User

namespace Potassco::Test {

TEST_CASE("Assertion and Error", "[error]") {
    namespace CM         = Catch::Matchers;
    auto makeError       = [](std::errc ec = std::errc::invalid_argument) { return detail::translateEc(ec); };
    auto messageContains = [](const std::string& s) { return CM::MessageMatches(CM::ContainsSubstring(s)); };
    auto messageEquals   = [](const std::string& s) { return CM::MessageMatches(CM::Equals(s)); };
    auto makeLocation    = [](const std::source_location& loc, bool includeFile, const char* m = "") -> std::string {
        std::ostringstream os;
        if (not includeFile)
            os << loc.function_name() << ':' << loc.line() << ": " << m;
        else
            os << ExpressionInfo::relativeFileName(loc) << ':' << loc.line() << ": " << loc.function_name() << ": "
               << m;
        return std::move(os).str();
    };

    SECTION("fail type") {
        auto e = POTASSCO_CAPTURE_EXPRESSION(expression);
        SECTION("recoverable error") {
            auto loc        = makeLocation(e.location, false);
            auto defMessage = std::make_error_code(std::errc::invalid_argument).message();

            SECTION("no message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e), std::invalid_argument,
                                     messageEquals(defMessage + "\n" + loc + "check 'expression' failed."));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e), std::invalid_argument,
                                     messageEquals(defMessage + "\n" + loc + "failed."));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e, "custom message"), std::invalid_argument,
                                     messageEquals(std::string("custom message: ") + defMessage + "\n" + loc +
                                                   "check 'expression' failed."));

                CHECK_THROWS_MATCHES(Potassco::failThrow(makeError(), e, "custom message with args %u %s", 1, "bla"),
                                     std::invalid_argument,
                                     messageEquals(std::string("custom message with args 1 bla: ") + defMessage + "\n" +
                                                   loc + "check 'expression' failed."));
            }
        }

        SECTION("precondition") {
            auto loc = makeLocation(e.location, false);
            SECTION("no message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(Errc::precondition_fail, e), std::invalid_argument,
                                     messageEquals(loc + "Precondition 'expression' failed."));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failThrow(Errc::precondition_fail, e), std::invalid_argument,
                                     messageEquals(loc + "Precondition failed."));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(Potassco::failThrow(Errc::precondition_fail, e, "custom message"),
                                     std::invalid_argument,
                                     messageEquals(loc + "Precondition 'expression' failed.\n"
                                                         "message: custom message"));

                CHECK_THROWS_MATCHES(
                    Potassco::failThrow(Errc::precondition_fail, e, "custom message with args %u %s", 1, "bla"),
                    std::invalid_argument,
                    messageEquals(loc + "Precondition 'expression' failed.\n"
                                        "message: custom message with args 1 bla"));
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
                                     messageEquals(loc + "Assertion 'expression' failed."));
            }
            SECTION("no expression") {
                e.expression = {};
                CHECK_THROWS_MATCHES(Potassco::failAbort(e), std::logic_error,
                                     messageEquals(loc + "Assertion failed."));
            }
            SECTION("with message") {
                CHECK_THROWS_MATCHES(Potassco::failAbort(e, "custom message"), std::logic_error,
                                     messageEquals(loc + "Assertion 'expression' failed.\n"
                                                         "message: custom message"));

                CHECK_THROWS_MATCHES(Potassco::failAbort(e, "custom message with args %u %s", 1, "bla"),
                                     std::logic_error,
                                     messageEquals(loc + "Assertion 'expression' failed.\n"
                                                         "message: custom message with args 1 bla"));
            }
        }
    }

    SECTION("fail ec") {
        auto           e         = POTASSCO_CAPTURE_EXPRESSION(expression);
        constexpr auto errcMatch = [](Errc lhs, std::errc rhs) {
            return static_cast<std::underlying_type_t<Errc>>(lhs) ==
                   static_cast<std::underlying_type_t<std::errc>>(rhs);
        };
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
            REQUIRE_THROWS_AS(failThrow(detail::translateEc(EINTR), e, "my message"), RuntimeError);

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

} // namespace Potassco::Test
