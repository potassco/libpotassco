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

#include <system_error>
/*!
 * \addtogroup BasicTypes
 */
///@{
namespace Potassco {

enum class Errc : std::underlying_type_t<std::errc> {
    precondition_fail = -1,
    // std::bad_alloc
    bad_alloc = std::underlying_type_t<std::errc>(std::errc::not_enough_memory),
    // standard logic errors
    length_error     = std::underlying_type_t<std::errc>(std::errc::argument_list_too_long),
    invalid_argument = std::underlying_type_t<std::errc>(std::errc::invalid_argument),
    domain_error     = std::underlying_type_t<std::errc>(std::errc::argument_out_of_domain),
    out_of_range     = std::underlying_type_t<std::errc>(std::errc::result_out_of_range),
    // standard runtime errors
    overflow_error = std::underlying_type_t<std::errc>(std::errc::value_too_large),
};

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(Errc ec, const ExpressionInfo& location, const char* message)
        : std::runtime_error(message)
        , expression_(location.expression)
        , location_(location.location)
        , errc_(ec) {}

    [[nodiscard]] Errc                        errc() const noexcept { return errc_; }
    [[nodiscard]] const std::source_location& location() const noexcept { return location_; }
    [[nodiscard]] const char*                 expression() const noexcept { return expression_.c_str(); }
    [[nodiscard]] std::string                 details() const;

private:
    std::string          expression_;
    std::source_location location_;
    Errc                 errc_;
};

template <typename ActionT>
struct AtScopeExit {
    ~AtScopeExit() noexcept(false) { action(); }
    ActionT action;
};
template <typename ActionT>
AtScopeExit(ActionT) -> AtScopeExit<ActionT>;

//! Helper macro for executing actions on scope exit.
/*!
 * POTASSCO_SCOPE_EXIT can be used to ensure cleanup even in case of exceptions.
 * E.g.
 * \code
 *   auto* f = fopen("...");
 *   POTASSCO_SCOPE_EXIT({ fclose(f); });
 *   // do stuff that might throw
 *   return;
 * \endcode
 */
#define POTASSCO_SCOPE_EXIT(...)                                                                                       \
    Potassco::AtScopeExit POTASSCO_CONCAT(e, __COUNTER__) { [&]() __VA_ARGS__ }

namespace detail {
template <typename T>
constexpr auto translateEc(T in) {
    if constexpr (std::is_same_v<T, int>)
        return static_cast<Potassco::Errc>(in >= 0 ? in : -in);
    else if constexpr (std::is_same_v<T, std::errc>)
        return static_cast<Potassco::Errc>(in);
    else
        return in;
}
} // namespace detail

#if (defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL == 1) || (not defined(_MSC_VER) && __cplusplus < 202002L)
#define POTASSCO_OPTARGS(...) , ##__VA_ARGS__
#else
#define POTASSCO_OPTARGS(...) __VA_OPT__(, ) __VA_ARGS__
#endif

//! Throws an exception of type defined the given error code.
POTASSCO_ATTR_NORETURN extern void failThrow(Errc ec, const ExpressionInfo& expressionInfo, const char* fmt = nullptr,
                                             ...) POTASSCO_ATTRIBUTE_FORMAT(3, 4);

//! Calls the currently active abort handler.
/*!
 * \see Potassco::setAbortHandler(AbortHandler handler).
 */
POTASSCO_ATTR_NORETURN extern void failAbort(const ExpressionInfo& expressionInfo, const char* fmt = nullptr, ...)
    POTASSCO_ATTRIBUTE_FORMAT(2, 3);

//! Evaluates the given expression and calls Potassco::failAbort() if it is false.
/*!
 * \note The given expression is *always* evaluated. Use POTASSCO_DEBUG_ASSERT for debug-only checks.
 *
 * \param exp Expression that shall be true.
 * \param ... An optional message that is added to the error output on failure. The message can be a C-style format
 *            string followed by corresponding arguments.
 */
#define POTASSCO_ASSERT(exp, ...)                                                                                      \
    (void) ((!!(exp)) || (Potassco::failAbort(POTASSCO_CAPTURE_EXPRESSION(exp) POTASSCO_OPTARGS(__VA_ARGS__)), 0))

//! Evaluates the given expression and calls failThrow(code, ...) with the given error code if it is false.
/*!
 * \note On failure, Potassco::failThrow(code, ...) is called if `code` is of type int,  std::errc, or Errc.
 *       Otherwise, failThrow(code, ...) must be a viable function found via ADL.
 *
 * \param exp  Expression that is expected to be true.
 * \param code An error code describing the error if `exp` is false.
 * \param ...  Optional parameters passed to the selected failThrow() overload on error.
 */
#define POTASSCO_CHECK(exp, code, ...)                                                                                 \
    (void) ((!!(exp)) || (failThrow(Potassco::detail::translateEc((code)),                                             \
                                    POTASSCO_CAPTURE_EXPRESSION(exp) POTASSCO_OPTARGS(__VA_ARGS__)),                   \
                          0))

//! Effect: POTASSCO_CHECK(false, code, ...)
#define POTASSCO_FAIL(code, ...)                                                                                       \
    ((void) (failThrow(Potassco::detail::translateEc((code)),                                                          \
                       {{}, POTASSCO_CURRENT_LOCATION()} POTASSCO_OPTARGS(__VA_ARGS__)),                               \
             0))

//! Evaluates the given expression and calls Potassco::failThrow(Errc::precondition_fail, ...) if it is false.
/*!
 * \note The given expression is *always* evaluated. Use POTASSCO_DEBUG_CHECK_PRE for debug-only checks.
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

#ifdef NDEBUG
#define POTASSCO_DEBUG_ASSERT(exp, ...)    static_cast<void>(0)
#define POTASSCO_DEBUG_CHECK_PRE(exp, ...) static_cast<void>(0)
#else
#define POTASSCO_DEBUG_ASSERT(exp, ...)    POTASSCO_ASSERT(exp, __VA_ARGS__)
#define POTASSCO_DEBUG_CHECK_PRE(exp, ...) POTASSCO_CHECK_PRE(exp, __VA_ARGS__)
#endif

///@}
} // namespace Potassco
