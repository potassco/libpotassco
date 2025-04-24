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
#pragma once
#include <potassco/platform.h>

#include <potassco/enum.h>

#include <system_error>
namespace Potassco {
/*!
 * \addtogroup BasicTypes
 */
///@{

enum class Errc : std::underlying_type_t<std::errc> {
    precondition_fail = -1,
    // std::bad_alloc
    bad_alloc = to_underlying(std::errc::not_enough_memory),
    // standard logic errors
    length_error     = to_underlying(std::errc::argument_list_too_long),
    invalid_argument = to_underlying(std::errc::invalid_argument),
    domain_error     = to_underlying(std::errc::argument_out_of_domain),
    out_of_range     = to_underlying(std::errc::result_out_of_range),
    // standard runtime errors
    overflow_error = to_underlying(std::errc::value_too_large),
};

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(Errc ec, const std::source_location& location, const char* message)
        : std::runtime_error(message)
        , location_(location)
        , errc_(ec) {}

    [[nodiscard]] Errc                        errc() const noexcept { return errc_; }
    [[nodiscard]] const std::source_location& location() const noexcept { return location_; }
    [[nodiscard]] std::string_view            message() const noexcept;
    [[nodiscard]] std::string_view            details() const noexcept;

private:
    std::source_location location_;
    Errc                 errc_;
};

template <typename ActionT>
struct AtScopeExit {
    ~AtScopeExit() noexcept(false) { action(); }
    ActionT action;
};
template <typename ActionT>
AtScopeExit(ActionT) -> AtScopeExit<ActionT>; // NOLINT

//! Helper macro for executing actions on scope exit.
/*!
 * POTASSCO_SCOPE_EXIT can be used to ensure cleanup even in the case of exceptions.
 * E.g.,
 * \code
 *   auto* f = fopen("...");
 *   POTASSCO_SCOPE_EXIT({ fclose(f); });
 *   // do stuff that might throw
 *   return;
 * \endcode
 */
#define POTASSCO_SCOPE_EXIT(...)                                                                                       \
    Potassco::AtScopeExit POTASSCO_CONCAT(e, __COUNTER__) { [&]() __VA_ARGS__ }

namespace Detail {
template <typename T>
constexpr auto translateEc(T in) {
    if constexpr (std::is_same_v<T, int>) {
        return static_cast<Potassco::Errc>(in >= 0 ? in : -in);
    }
    else if constexpr (std::is_same_v<T, std::errc>) {
        return static_cast<Potassco::Errc>(in);
    }
    else {
        return in;
    }
}
} // namespace Detail

//! Throws an exception of type defined the given error code.
POTASSCO_ATTR_NORETURN extern void failThrow(Errc ec, const ExpressionInfo& expressionInfo, const char* fmt = nullptr,
                                             ...) POTASSCO_ATTRIBUTE_FORMAT(3, 4);

//! Calls the currently active abort handler.
/*!
 * \see Potassco::setAbortHandler(AbortHandler handler).
 */
POTASSCO_ATTR_NORETURN extern void failAbort(const ExpressionInfo& expressionInfo, const char* fmt = nullptr, ...)
    POTASSCO_ATTRIBUTE_FORMAT(2, 3);

//! Evaluates the given expression and calls `Potassco::failAbort()` if it is false.
/*!
 * \note The given expression is @b always evaluated. Use `POTASSCO_DEBUG_ASSERT()` for debug-only checks.
 *
 * \param exp Expression that shall be true.
 * \param ... An optional message that is added to the error output on failure. The message can be a C-style format
 *            string followed by corresponding arguments.
 */
#define POTASSCO_ASSERT(exp, ...)                                                                                      \
    (void) ((!!(exp)) || (Potassco::failAbort(POTASSCO_CAPTURE_EXPRESSION(exp) POTASSCO_OPTARGS(__VA_ARGS__)), 0))

//! Evaluates the given expression and calls failThrow(code, ...) with the given error code if it is false.
/*!
 * \note On failure, Potassco::failThrow(code, ...) is called if `code` is of type int, std::errc, or Errc.
 *       Otherwise, failThrow(code, ...) must be a viable function found via ADL.
 *
 * \param exp  Expression that is expected to be true.
 * \param code An error code describing the error if `exp` is false.
 * \param ...  Optional parameters passed to the selected failThrow() overload on error.
 */
#define POTASSCO_CHECK(exp, code, ...)                                                                                 \
    (void) ((!!(exp)) || (failThrow(Potassco::Detail::translateEc((code)),                                             \
                                    POTASSCO_CAPTURE_EXPRESSION(exp) POTASSCO_OPTARGS(__VA_ARGS__)),                   \
                          0))

//! Effect: POTASSCO_CHECK(false, code, ...)
#define POTASSCO_FAIL(code, ...)                                                                                       \
    ((void) (failThrow(Potassco::Detail::translateEc((code)),                                                          \
                       {{}, POTASSCO_CURRENT_LOCATION()} POTASSCO_OPTARGS(__VA_ARGS__)),                               \
             0))

//! Evaluates the given expression and calls Potassco::failThrow(Errc::precondition_fail, ...) if it is false.
/*!
 * \note The given expression is @b always evaluated. Use `POTASSCO_DEBUG_CHECK_PRE()` for debug-only checks.
 * \note By default, precondition failures are mapped to std::invalid_argument exceptions.
 *
 * \param exp Expression that shall be true.
 * \param ... An optional message that is added to the error output on failure. The message can be a C-style format
 *            string followed by corresponding arguments.
 */
#define POTASSCO_CHECK_PRE(exp, ...) POTASSCO_CHECK(exp, Potassco::Errc::precondition_fail, __VA_ARGS__)

//! Effect: POTASSCO_ASSERT(false, Msg, ...)
#define POTASSCO_ASSERT_NOT_REACHED(Msg, ...)                                                                          \
    Potassco::failAbort(POTASSCO_CAPTURE_EXPRESSION(not reached), Msg POTASSCO_OPTARGS(__VA_ARGS__))

/*!
 * \def POTASSCO_DEBUG_ASSERT(exp, ...)
 * Like POTASSCO_ASSERT but only evaluated if `NDEBUG` is not defined.
 */

/*!
 * \def POTASSCO_DEBUG_CHECK_PRE(exp, ...)
 * Like POTASSCO_CHECK_PRE but only evaluated if `NDEBUG` is not defined.
 */

#ifdef NDEBUG
#define POTASSCO_DEBUG_ASSERT(exp, ...)    static_cast<void>(0)
#define POTASSCO_DEBUG_CHECK_PRE(exp, ...) static_cast<void>(0)
#else
#define POTASSCO_DEBUG_ASSERT(exp, ...)    POTASSCO_ASSERT(exp, __VA_ARGS__)
#define POTASSCO_DEBUG_CHECK_PRE(exp, ...) POTASSCO_CHECK_PRE(exp, __VA_ARGS__)
#endif

template <std::integral To, std::integral From>
constexpr To safe_cast(From from) {
    POTASSCO_CHECK(std::in_range<To>(from), Errc::out_of_range);
    return static_cast<To>(from);
}
template <std::integral To, typename From>
requires(std::is_enum_v<From>)
constexpr To safe_cast(From from) {
    return safe_cast<To>(to_underlying(from));
}
template <std::integral To = uint32_t, typename C>
constexpr To size_cast(const C& c) {
    static_assert(std::is_unsigned_v<decltype(c.size())>, "unsigned size expected");
    if constexpr (std::is_unsigned_v<To> && sizeof(To) >= sizeof(decltype(c.size()))) {
        return static_cast<To>(c.size());
    }
    else {
        return safe_cast<To>(c.size());
    }
}

///@}
} // namespace Potassco
