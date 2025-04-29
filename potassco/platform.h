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

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <source_location>
#include <string_view>

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
#define POTASSCO_APPLY_PRAGMA(x)          _Pragma(#x)
#define POTASSCO_PRAGMA_TODO(x)           POTASSCO_APPLY_PRAGMA(message("TODO: " #x))
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap) __attribute__((__format__(__printf__, fp, ap)))
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wvariadic-macros"
#define POTASSCO_WARNING_PUSH()          _Pragma("clang diagnostic push")
#define POTASSCO_WARNING_POP()           _Pragma("clang diagnostic pop")
#define POTASSCO_WARNING_IGNORE_CLANG(X) _Pragma(POTASSCO_STRING(clang diagnostic ignored X))
#define POTASSCO_WARNING_BEGIN_RELAXED                                                                                 \
    POTASSCO_WARNING_PUSH()                                                                                            \
    POTASSCO_WARNING_IGNORE_CLANG("-Wzero-length-array") POTASSCO_WARNING_IGNORE_CLANG("-Wsign-conversion")
#define POTASSCO_WARNING_END_RELAXED POTASSCO_WARNING_POP()
#define POTASSCO_ATTR_INLINE         [[clang::always_inline]]
#else
#pragma GCC diagnostic push
#pragma GCC system_header
#define POTASSCO_WARNING_PUSH()        _Pragma("GCC diagnostic push") POTASSCO_WARNING_IGNORE_GCC("-Wpragmas")
#define POTASSCO_WARNING_POP()         _Pragma("GCC diagnostic pop")
#define POTASSCO_WARNING_IGNORE_GCC(X) _Pragma(POTASSCO_STRING(GCC diagnostic ignored X))
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
#ifndef POTASSCO_WARNING_IGNORE_MSVC
#define POTASSCO_WARNING_IGNORE_MSVC(...)
#endif

#define POTASSCO_WARNING_IGNORE_GNU(X) POTASSCO_WARNING_IGNORE_GCC(X) POTASSCO_WARNING_IGNORE_CLANG(X)

static_assert(UINTPTR_MAX <= UINT64_MAX, "Unsupported platform!");

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
    static const char* relativeFileName(const std::source_location& loc);
};
#define POTASSCO_CURRENT_LOCATION() std::source_location::current()
#define POTASSCO_CAPTURE_EXPRESSION(E)                                                                                 \
    Potassco::ExpressionInfo { .expression = #E, .location = POTASSCO_CURRENT_LOCATION() }

using AbortHandler = void (*)(const char* msg);
//! Sets handler as the new abort handler and returns the previously installed handler.
/*!
 * \note A given handler shall either abort the program or throw an exception. If no handler is set, `std::abort()` is
 *       used as the abort handler.
 */
extern AbortHandler setAbortHandler(AbortHandler handler);

//! Sets x87 floating-point unit to double precision if needed and returns the previous configuration.
/*!
 * \note This function does nothing (and returns 0) if the x87 floating-point unit is not active.
 * \note x87 floating-point unit is typically only used on x86_32 (x86_64 uses SSE by default).
 * \return The previous configuration or UINT32_MAX if configuration failed.
 */
unsigned initFpuPrecision();
//! Restores x87 floating-point unit to a previous configuration `r` returned from initFpuPrecision().
void restoreFpuPrecision(unsigned r);

} // namespace Potassco

///@}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
