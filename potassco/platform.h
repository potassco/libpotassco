//
// Copyright (c) 2016 - present, Benjamin Kaufmann
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

#include <cstdint>
#include <cstdlib>
#include <new>
#include <source_location>
#include <string_view>
#include <system_error>

#define POTASSCO_STRING2(x)    #x
#define POTASSCO_STRING(x)     POTASSCO_STRING2(x)
#define POTASSCO_CONCAT2(X, Y) X##Y
#define POTASSCO_CONCAT(X, Y)  POTASSCO_CONCAT2(X, Y)

#define POTASSCO_ATTR_NORETURN [[noreturn]]
#if defined(_MSC_VER)
#define POTASSCO_ATTR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define POTASSCO_ATTR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#if (defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL == 1) || (not defined(_MSC_VER) && __cplusplus < 202002L)
#define POTASSCO_OPTARGS(...) , ##__VA_ARGS__
#else
#define POTASSCO_OPTARGS(...) __VA_OPT__(, ) __VA_ARGS__
#endif

#define POTASSCO_PRAGMA(X) _Pragma(#X)

#if defined(_MSC_VER)
#define POTASSCO_WARNING_PUSH()         __pragma(warning(push))
#define POTASSCO_WARNING_POP()          __pragma(warning(pop))
#define POTASSCO_WARNING_IGNORE_MSVC(X) __pragma(warning(disable : X))
#define POTASSCO_PRAGMA_TODO(X)         __pragma(message(__FILE__ "(" POTASSCO_STRING(__LINE__) ") : TODO: " X))
#define POTASSCO_FUNC_NAME              __FUNCTION__
#define POTASSCO_WARNING_BEGIN_RELAXED  POTASSCO_WARNING_PUSH() POTASSCO_WARNING_IGNORE_MSVC(4200)
#define POTASSCO_WARNING_END_RELAXED    POTASSCO_WARNING_POP()
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap)
#define POTASSCO_ATTR_INLINE [[msvc::forceinline]]

#elif defined(__GNUC__) || defined(__clang__)
#if !defined(__STDC_FORMAT_MACROS)
#define __STDC_FORMAT_MACROS
#endif
#if !defined(__STDC_LIMIT_MACROS)
#define __STDC_LIMIT_MACROS
#endif
#define POTASSCO_FUNC_NAME                __PRETTY_FUNCTION__
#define POTASSCO_PRAGMA_TODO(X)           POTASSCO_PRAGMA(message("TODO: " X))
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap) __attribute__((__format__(__printf__, fp, ap)))
#define POTASSCO_PREFETCH(address, ...)   __builtin_prefetch(address POTASSCO_OPTARGS(__VA_ARGS__))
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wvariadic-macros"
#define POTASSCO_PRAGMA_CLANG(X)         POTASSCO_PRAGMA(clang X)
#define POTASSCO_WARNING_PUSH()          POTASSCO_PRAGMA_CLANG(diagnostic push)
#define POTASSCO_WARNING_POP()           POTASSCO_PRAGMA_CLANG(diagnostic pop)
#define POTASSCO_WARNING_IGNORE_CLANG(X) POTASSCO_PRAGMA_CLANG(diagnostic ignored X)
#define POTASSCO_WARNING_BEGIN_RELAXED                                                                                 \
    POTASSCO_WARNING_PUSH()                                                                                            \
    POTASSCO_WARNING_IGNORE_CLANG("-Wzero-length-array")                                                               \
    POTASSCO_WARNING_IGNORE_CLANG("-Wsign-conversion")                                                                 \
    POTASSCO_WARNING_IGNORE_CLANG("-Wpedantic")
#define POTASSCO_WARNING_END_RELAXED POTASSCO_WARNING_POP()
#define POTASSCO_ATTR_INLINE         [[clang::always_inline]]
#else
#pragma GCC diagnostic push
#pragma GCC system_header
#define POTASSCO_PRAGMA_GCC(X)         POTASSCO_PRAGMA(GCC X)
#define POTASSCO_WARNING_PUSH()        POTASSCO_PRAGMA_GCC(diagnostic push) POTASSCO_WARNING_IGNORE_GCC("-Wpragmas")
#define POTASSCO_WARNING_POP()         POTASSCO_PRAGMA_GCC(diagnostic pop)
#define POTASSCO_WARNING_IGNORE_GCC(X) POTASSCO_PRAGMA_GCC(diagnostic ignored X)
#define POTASSCO_WARNING_BEGIN_RELAXED                                                                                 \
    POTASSCO_WARNING_PUSH()                                                                                            \
    POTASSCO_WARNING_IGNORE_GCC("-Wpedantic")                                                                          \
    POTASSCO_WARNING_IGNORE_GCC("-pedantic")                                                                           \
    POTASSCO_WARNING_IGNORE_GCC("-Wsign-conversion")
#define POTASSCO_WARNING_END_RELAXED POTASSCO_WARNING_POP()
#define POTASSCO_ATTR_INLINE         [[gnu::always_inline]]
#endif
#else
#define POTASSCO_FUNC_NAME __FILE__
#define POTASSCO_WARNING_BEGIN_RELAXED
#define POTASSCO_WARNING_END_RELAXED
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap)
#define POTASSCO_ATTR_INLINE
#endif

#define POTASSCO_FORCE_INLINE POTASSCO_ATTR_INLINE inline

#if !defined(POTASSCO_ENABLE_PRAGMA_TODO) || POTASSCO_ENABLE_PRAGMA_TODO == 0
#undef POTASSCO_PRAGMA_TODO
#define POTASSCO_PRAGMA_TODO(X)
#endif
#if !defined(POTASSCO_WARNING_IGNORE_GCC)
#define POTASSCO_WARNING_IGNORE_GCC(...)
#endif
#if !defined(POTASSCO_WARNING_IGNORE_CLANG)
#define POTASSCO_WARNING_IGNORE_CLANG(...)
#endif
#if !defined(POTASSCO_WARNING_IGNORE_MSVC)
#define POTASSCO_WARNING_IGNORE_MSVC(...)
#endif
#if !defined(POTASSCO_PREFETCH)
#define POTASSCO_PREFETCH(...)
#endif

#define POTASSCO_WARNING_IGNORE_GNU(X) POTASSCO_WARNING_IGNORE_GCC(X) POTASSCO_WARNING_IGNORE_CLANG(X)

static_assert(UINTPTR_MAX <= UINT64_MAX, "Unsupported platform!");
#define PRIsv     ".*s"
#define PRI_SV(v) static_cast<int>((v).size()), (v).data()

/*!
 * \addtogroup BasicTypes
 */
///@{
namespace Potassco {
struct ExpressionInfo {
    std::string_view     expression;
    std::source_location location;
    //! Returns `loc's` `file_name()` relative to source root directory.
    /*!
     * \note If the given location is not from the same source tree, `file_name()` is returned unmodified.
     */
    static auto relativeFileName(const std::source_location& loc) -> const char*;
};
#define POTASSCO_CURRENT_LOCATION() std::source_location::current()
#define POTASSCO_CAPTURE_EXPRESSION(E)                                                                                 \
    Potassco::ExpressionInfo { .expression = #E, .location = POTASSCO_CURRENT_LOCATION() }

POTASSCO_WARNING_PUSH()
POTASSCO_WARNING_IGNORE_MSVC(4722) // destructor never returns, potential memory leak
template <typename ActionT>
struct AtScopeExit {
    ~AtScopeExit() noexcept(false) { action(); }
    ActionT action;
};
POTASSCO_WARNING_POP()

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
    POTASSCO_WARNING_BEGIN_RELAXED                                                                                     \
    Potassco::AtScopeExit POTASSCO_CONCAT(e, __COUNTER__) {                                                            \
        [&] { __VA_ARGS__ POTASSCO_WARNING_END_RELAXED }                                                               \
    }

enum class Errc {
    precondition_fail = -1,
    // std::bad_alloc
    bad_alloc = static_cast<int>(std::errc::not_enough_memory),
    // standard logic errors
    length_error     = static_cast<int>(std::errc::argument_list_too_long),
    invalid_argument = static_cast<int>(std::errc::invalid_argument),
    domain_error     = static_cast<int>(std::errc::argument_out_of_domain),
    out_of_range     = static_cast<int>(std::errc::result_out_of_range),
    // standard runtime errors
    overflow_error = static_cast<int>(std::errc::value_too_large),
};

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
    (void) ((!!(exp)) ||                                                                                               \
            (failThrow(Potassco::translateEc((code)), POTASSCO_CAPTURE_EXPRESSION(exp) POTASSCO_OPTARGS(__VA_ARGS__)), \
             0))

//! Effect: POTASSCO_CHECK(false, code, ...)
#define POTASSCO_FAIL(code, ...)                                                                                       \
    ((void) (failThrow(Potassco::translateEc((code)),                                                                  \
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

//! Sets x87 floating-point unit to double precision if needed and returns the previous configuration.
/*!
 * \note This function does nothing (and returns 0) if the x87 floating-point unit is not active.
 * \note x87 floating-point unit is typically only used on x86_32 (x86_64 uses SSE by default).
 * \return The previous configuration or UINT32_MAX if configuration failed.
 */
auto initFpuPrecision() -> unsigned;
//! Restores x87 floating-point unit to a previous configuration `r` returned from initFpuPrecision().
void restoreFpuPrecision(unsigned r);

//! Enables ANSI color support for the given file (if possible).
/*!
 * \return
 *   - std::errc{} if color support was successfully enabled,
 *   - std::errc::inappropriate_io_control_operation if the given file is not a terminal,
 *   - std::errc::function_not_supported if the platform does not support ANSI colors.
 */
auto enableAnsiColorSupport(FILE* file) -> std::errc;
bool isTerminal(FILE* file);
void lockFile(FILE* file);
void unlockFile(FILE* file);

using AlarmFunc = void (*)(int);
//! Platform wrapper for setitimer() function.
/*!
 * Sets a global timer that calls the given function `f` after `millis` milliseconds.
 * \note Calling setAlarm() overrides any previously set alarm.
 */
auto setAlarm(uint32_t millis, AlarmFunc f) -> std::errc;
//! Kills any pending alarm previously set by setAlarm().
auto killAlarm() -> bool;

//! Gets the total (user + system) time in seconds spent by the current process.
/*!
 * \note The function returns a (quiet) NaN if the information is not available on the current platform.
 */
auto getProcessTime() -> double;
//! Gets the total (user + system) time in seconds spent by the current thread.
/*!
 * \note The function returns a (quiet) NaN if the information is not available on the current platform.
 */
auto getThreadTime() -> double;

//! Abstraction over global "malloc-like" allocation functions.
struct SystemAllocator {
    static constexpr auto realloc_max_align = static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__);
    static constexpr auto no_size_info      = static_cast<std::size_t>(-1);
    static constexpr auto default_align     = static_cast<std::align_val_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__);

    //! Rounds the given size up to a value that minimizes padding.
    /*!
     * \note This is a "best effort" function that might fall back to always returning the input size.
     * \param sz The allocation size to round up.
     * \param align The alignment requirements for the allocation.
     * \return A value >= sz.
     */
    static auto goodAllocSize(std::size_t sz, std::align_val_t align = default_align) -> std::size_t;

    //! Allocates `sz` number of bytes with the given alignment.
    /*!
     * \pre sz > 0.
     * \param sz The number of bytes to allocate.
     * \param align The alignment requirements for the allocation.
     * \return A suitable aligned block of memory of `sz` bytes.
     * \throw std::bad_alloc if memory allocation failed.
     */
    static auto allocate(std::size_t sz, std::align_val_t align = default_align) -> void*;

    //! Reallocates the given area of memory, which must be null or have been allocated via a call to `allocate`.
    /*!
     * \pre `mem` is null or was previously allocated via `allocate()`.
     * \pre Any objects stored in `mem` must be "relocatable" (bitwise movable) and their alignment must not exceed
     *      `realloc_max_align`.
     * \param mem The memory block to reallocate.
     * \param sz The new size.
     * \return On success, a new block of memory of `sz` bytes. On failure, `mem` remains valid.
     * \throw std::bad_alloc if memory allocation failed.
     */
    static auto reallocate(void* mem, std::size_t sz) -> void*;

    //! Frees the given memory block, which must have been allocated via a call to `allocate` or `reallocate`.
    static void deallocate(void* mem, std::size_t sz = no_size_info, std::align_val_t align = default_align);

    //! Tries to expand the given memory block to the new size without relocation.
    /*!
     * \pre `mem` was previously allocated via `allocate()` and `sz` is not less than the size that was allocated.
     * \param mem The memory block to expand.
     * \param align The alignment requirements for the allocation.
     * \param[inout] sz The new minimal size.
     * \return On success, the function returns the new size of the memory block, which is no less than `sz`. Otherwise,
     *         the function returns 0.
     */
    static auto tryExpand(void* mem, std::size_t sz, std::align_val_t align = default_align) -> std::size_t;
};

} // namespace Potassco

///@}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
