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

namespace Potassco {
/*!
 * \addtogroup BasicTypes
 */
///@{

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
using AbortHandler = void (*)(const char* msg);
//! Sets handler as the new abort handler and returns the previously installed handler.
/*!
 * \note A given handler shall either abort the program or throw an exception. If no handler is set, `std::abort()` is
 *       used as the abort handler.
 */
extern auto setAbortHandler(AbortHandler handler) -> AbortHandler;
///@}
} // namespace Potassco
