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

#include <potassco/enum.h>

#include <fmt/core.h>

#include <iterator>
#include <type_traits>

template <Potassco::HasEnumEntries EnumT>
struct fmt::formatter<EnumT> : fmt::formatter<std::underlying_type_t<EnumT>> {
    auto format(EnumT enumT, auto& ctx) const {
        if (auto name = Potassco::enum_name(enumT); not name.empty()) {
            return fmt::format_to(ctx.out(), "{}", name);
        }
        return fmt::formatter<std::underlying_type_t<EnumT>>::format(Potassco::to_underlying(enumT), ctx);
    }
};

namespace Potassco {
template <typename T>
concept CharBuffer = requires(T buffer, char c) {
    typename T::value_type;
    buffer.push_back(c);
};

template <CharBuffer C, typename... Args>
auto formatTo(C& buffer, fmt::format_string<Args...> format, Args&&... args) -> decltype(auto) {
    return fmt::vformat_to(std::back_inserter(buffer), format.get(), fmt::make_format_args(args...));
}

template <CharBuffer C, fmt::formattable T>
C& toChars(C& buffer, const T& value) {
    fmt::format_to(std::back_inserter(buffer), "{}", value);
    return buffer;
}

} // namespace Potassco
