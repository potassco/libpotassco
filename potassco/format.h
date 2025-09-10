//
// Copyright (c) 2025 - present, Benjamin Kaufmann
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
#include "basic_types.h"

#include <potassco/enum.h>

#include <cstdarg>
#include <string>
#include <type_traits>
namespace Potassco {
class DynamicBuffer;
namespace Detail {
char*       writeSigned(char* first, char* last, std::intmax_t);
char*       writeUnsigned(char* first, char* last, std::uintmax_t);
char*       writeFloat(char* first, char* last, double);
std::size_t vFormatf(DynamicBuffer&, const char* fmt, va_list args) noexcept POTASSCO_ATTRIBUTE_FORMAT(2, 0);
} // namespace Detail
///////////////////////////////////////////////////////////////////////////////
// T -> chars
///////////////////////////////////////////////////////////////////////////////
template <typename T>
concept CharBuffer = requires(T buffer, std::string_view v) {
    { buffer.append(v) } -> std::convertible_to<T&>;
};

template <CharBuffer S>
S& toChars(S& out, const char* in) {
    return out.append(in ? std::string_view(in) : std::string_view());
}
template <CharBuffer S>
S& toChars(S& out, const std::string& s) {
    return out.append(std::string_view(s));
}
template <CharBuffer S>
S& toChars(S& out, std::string_view s) {
    return out.append(s);
}
template <CharBuffer S>
S& toChars(S& out, bool b) {
    return out.append(std::string_view(b ? "true" : "false"));
}

template <CharBuffer S, std::integral T>
S& toChars(S& out, T in) {
    char  temp[128];
    char* end;
    if constexpr (std::is_unsigned_v<T>) {
        if (in == static_cast<T>(-1)) {
            return out.append(std::string_view("umax"));
        }
        end = Detail::writeUnsigned(std::begin(temp), std::end(temp), in);
    }
    else {
        end = Detail::writeSigned(std::begin(temp), std::end(temp), in);
    }
    return out.append(std::string_view{temp, end});
}
template <CharBuffer S, std::floating_point T>
S& toChars(S& out, T in) {
    char  temp[128];
    auto* end = Detail::writeFloat(std::begin(temp), std::end(temp), static_cast<double>(in));
    return out.append(std::string_view{temp, end});
}
template <CharBuffer S, HasEnumEntries EnumT>
S& toChars(S& out, EnumT enumT) {
    if (auto name = Potassco::enum_name(enumT); not name.empty()) {
        return out.append(name);
    }
    return toChars(out, to_underlying(enumT));
}

template <CharBuffer S, typename T, typename U>
S& toChars(S& out, const std::pair<T, U>& p, char sep = ',') {
    toChars(out, p.first).append(1, sep);
    return toChars(out, p.second);
}
template <CharBuffer S, typename C>
requires requires(S& s, C c) {
    c.begin();
    c.end();
    toChars(s, *c.begin());
}
S& toChars(S& out, const C& c, char sep = ',') {
    std::size_t n = 0;
    for (const auto& v : c) {
        out.append(std::string_view(&sep, std::exchange(n, 1)));
        toChars(out, v);
    }
    return out;
}
///////////////////////////////////////////////////////////////////////////////
// T -> string
///////////////////////////////////////////////////////////////////////////////
template <typename T>
requires requires(std::string& out, T& in) { toChars(out, in); }
constexpr std::string toString(const T& x) {
    std::string out;
    toChars(out, x);
    return out;
}
template <typename T, typename... Args>
std::string toString(const T& t, const Args&... args) {
    std::string res;
    toChars(res, t);
    if constexpr (sizeof...(Args) > 0) {
        std::ignore = (toChars(res.append(1, ','), args), ...);
    }
    return res;
}

class StrF { // NOLINT
public:
    [[nodiscard]] auto c_str() const noexcept -> const char*;
    [[nodiscard]] auto view() const noexcept -> std::string_view;

private:
    friend StrF formatF(const char* fmt, ...) noexcept POTASSCO_ATTRIBUTE_FORMAT(1, 2);
    StrF() = default;
    char          local_[128 - sizeof(DynamicBuffer)];
    DynamicBuffer buffer_{local_};
};
//! Formats the given arguments according to `fmt` and stores the result in `buffer`.
/*!
 * \note Formatting follows the rules of std::vsnprintf().
 * \return The number of bytes written to buffer.
 */
std::size_t formatTo(DynamicBuffer& buffer, const char* fmt, ...) noexcept POTASSCO_ATTRIBUTE_FORMAT(2, 3);
StrF        formatF(const char* fmt, ...) noexcept POTASSCO_ATTRIBUTE_FORMAT(1, 2);

} // namespace Potassco
