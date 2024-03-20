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
#define POTASSCO_PRAGMA_TODO(X)        __pragma(message(__FILE__ "(" POTASSCO_STRING(__LINE__) ") : TODO: " X))
#define POTASSCO_FUNC_NAME             __FUNCTION__
#define POTASSCO_WARNING_BEGIN_RELAXED __pragma(warning(push)) __pragma(warning(disable : 4200))

#define POTASSCO_WARNING_END_RELAXED __pragma(warning(pop))
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap)

#elif defined(__GNUC__) || defined(__clang__)
#if not defined(__STDC_FORMAT_MACROS)
#define __STDC_FORMAT_MACROS
#endif
#if not defined(__STDC_LIMIT_MACROS)
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
#define POTASSCO_WARNING_BEGIN_RELAXED                                                                                 \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wzero-length-array\"")
#define POTASSCO_WARNING_END_RELAXED _Pragma("clang diagnostic pop")
#else
#pragma GCC diagnostic push
#pragma GCC            system_header
#define POTASSCO_WARNING_BEGIN_RELAXED                                                                                 \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wpragmas\"")                                     \
        _Pragma("GCC diagnostic ignored \"-Wpedantic\"") _Pragma("GCC diagnostic ignored \"-pedantic\"")
#define POTASSCO_WARNING_END_RELAXED _Pragma("GCC diagnostic pop")
#endif
#else
#define POTASSCO_FUNC_NAME __FILE__
#define POTASSCO_WARNING_BEGIN_RELAXED
#define POTASSCO_WARNING_END_RELAXED
#define POTASSCO_ATTRIBUTE_FORMAT(fp, ap)
#endif

#if not defined(POTASSCO_ENABLE_PRAGMA_TODO) || POTASSCO_ENABLE_PRAGMA_TODO == 0
#undef POTASSCO_PRAGMA_TODO
#define POTASSCO_PRAGMA_TODO(X)
#endif

static_assert(UINTPTR_MAX <= UINT64_MAX, "Unsupported platform!");

/*!
 * \addtogroup BasicTypes
 */
///@{
namespace Potassco {
struct ExpressionInfo {
    std::string_view     expression;
    std::source_location location;
    //! Returns `loc`'s file_name() relative to source root directory.
    /*!
     * \note If the given location is not from the same source tree, file_name() is returned unmodified.
     */
    static const char* relativeFileName(const std::source_location& loc);
};
#define POTASSCO_CURRENT_LOCATION() std::source_location::current()
#define POTASSCO_CAPTURE_EXPRESSION(E)                                                                                 \
    Potassco::ExpressionInfo { .expression = #E, .location = POTASSCO_CURRENT_LOCATION() }

using AbortHandler = void (*)(const char* msg);
//! Sets handler as the new abort handler returns the previously installed handler.
/*!
 * \note If called, handler shall either abort the program or throw an exception. If handler is not set or is set to
 *       nullptr, std::abort() is used as the abort handler.
 */
extern AbortHandler setAbortHandler(AbortHandler handler);

} // namespace Potassco

///@}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
