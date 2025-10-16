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
#include <potassco/basic_types.h>
#include <potassco/enum.h>

#include <cstdarg>
#include <string>
#include <type_traits>
namespace Potassco {
struct Field;
namespace Detail {
char* writeSigned(char* first, char* last, std::intmax_t);
char* writeUnsigned(char* first, char* last, std::uintmax_t);
char* writeFloat(char* first, char* last, double, int p = -1);
void  writeField(DynamicBuffer& buffer, const Field& f);
struct TypeWithToChars {};
enum class AugmentStyle {
    quoted,
    keyed,
    styled,
};
std::size_t    vFormatTo(DynamicBuffer&, const char* fmt, va_list args) noexcept POTASSCO_ATTRIBUTE_FORMAT(2, 0);
constexpr bool hasValue(const auto&) { return true; }
template <typename T>
constexpr bool hasValue(const std::optional<T>& o) {
    return o.has_value();
}
auto resetStyle() -> std::string_view;
} // namespace Detail
///////////////////////////////////////////////////////////////////////////////
// T -> chars
///////////////////////////////////////////////////////////////////////////////
template <typename T>
concept CharBuffer = requires(T& buffer, std::string_view v) {
    { buffer.append(v) } -> std::convertible_to<T&>;
};
template <CharBuffer S>
constexpr S& toChars(S& out, std::string_view s) {
    return out.append(s);
}
template <CharBuffer S>
constexpr S& toChars(S& out, const std::string& s) {
    return out.append(static_cast<std::string_view>(s));
}
template <CharBuffer S>
constexpr S& toChars(S& out, const char* in) {
    return out.append(std::string_view(in));
}
template <CharBuffer S>
constexpr S& toChars(S& out, char c) {
    return out.append(std::string_view(&c, 1u));
}

template <CharBuffer S>
constexpr S& toChars(S& out, bool b) {
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
constexpr S& toChars(S& out, EnumT enumT) {
    if (auto name = Potassco::enum_name(enumT); not name.empty()) {
        return out.append(name);
    }
    using U = std::underlying_type_t<EnumT>;
    using T = std::conditional_t<not std::is_same_v<U, char>, U, int>;
    return toChars(out, static_cast<T>(enumT));
}
// Container types
template <CharBuffer S, typename T>
constexpr S& toChars(S& out, const std::optional<T>& in);
template <CharBuffer S, typename T, typename U>
constexpr S& toChars(S& out, const std::pair<T, U>& p, char sep = ',');
template <CharBuffer S, std::ranges::range C>
requires(not std::is_same_v<std::ranges::range_value_t<C>, char>)
constexpr S& toChars(S& out, const C& c, char sep = ',');

template <typename S, typename T>
POTASSCO_ATTR_INLINE constexpr S& toCharsChecked(S& out, const T& in) {
    if constexpr (requires { toChars(out, in); }) {
        toChars(out, in);
    }
    else {
        static_assert(std::is_same_v<T, Detail::TypeWithToChars>, "type must provide toChars");
    }
    return out;
}

template <CharBuffer S, typename T, typename U>
constexpr S& toChars(S& out, const std::pair<T, U>& p, char sep) {
    Potassco::toCharsChecked(out, p.first);
    return Potassco::toCharsChecked(out.append(std::string_view{&sep, 1}), p.second);
}
template <CharBuffer S, std::ranges::range C>
requires(not std::is_same_v<std::ranges::range_value_t<C>, char>)
constexpr S& toChars(S& out, const C& c, char sep) {
    for (std::size_t n = 0; const auto& v : c) {
        out.append(std::string_view(&sep, std::exchange(n, 1)));
        Potassco::toCharsChecked(out, v);
    }
    return out;
}
template <CharBuffer S, typename T>
constexpr S& toChars(S& out, const std::optional<T>& in) {
    return in.has_value() ? Potassco::toCharsChecked(out, *in) : out;
}
// Style types
template <typename T, Detail::AugmentStyle St = Detail::AugmentStyle::quoted>
struct Augmented {
    template <CharBuffer S>
    friend S& toChars(S& out, const Augmented& x) {
        if (x.ext.empty()) {
            return toChars(out, x.arg);
        }
        out.append(x.ext);
        if constexpr (St == Detail::AugmentStyle::keyed) {
            out.append(": ");
            toChars(out, x.arg);
        }
        else if constexpr (St == Detail::AugmentStyle::quoted) {
            toChars(out, x.arg);
            out.append(x.ext);
        }
        else if constexpr (St == Detail::AugmentStyle::styled) {
            toChars(out, x.arg);
            out.append(Detail::resetStyle());
        }
        return out;
    }
    std::string_view ext;
    const T&         arg;
};
template <typename T>
constexpr auto quoted(const T& arg, std::string_view q = "\"") -> Augmented<std::remove_cvref_t<T>> {
    return {q, arg};
}
template <typename T>
constexpr auto keyed(std::string_view key,
                     const T&         arg) -> Augmented<std::remove_cvref_t<T>, Detail::AugmentStyle::keyed> {
    return {key, arg};
}
struct Field {
    enum class Width : int {};
    static constexpr int8_t int_field  = -2;
    static constexpr int8_t uint_field = -3;
    static constexpr int8_t str_field  = -4;
    union F {
        std::intmax_t    i{};
        std::uintmax_t   u;
        double           d;
        std::string_view s;
    } f;
    int    width{0};        // minimum width; negative for left-justified
    int8_t prec{int_field}; // double precisions or field type
    char   term{0};         // term character
};
template <CharBuffer S>
S& toChars(S& out, const Field& f) {
    if constexpr (requires { out.append(f); }) {
        out.append(f);
    }
    else if (f.prec == Field::str_field) {
        constexpr auto pad = std::string_view{" "};
        auto           w   = f.f.s.size() + (f.term != 0);
        for (auto p = f.width > 0 ? static_cast<std::size_t>(f.width) : 0; p > w; --p) { out.append(pad); }
        out.append(f.f.s);
        out.append(std::string_view{&f.term, f.term != 0});
        for (auto p = -f.width > 0 ? static_cast<std::size_t>(-f.width) : 0; p > w; --p) { out.append(pad); }
    }
    else {
        char          local[256];
        DynamicBuffer temp{local};
        Detail::writeField(temp, f);
        out.append(temp.view());
    }
    return out;
}
template <std::integral T>
constexpr auto num(T n, Field::Width w, char term = 0) -> Field {
    auto rw = to_underlying(w);
    if constexpr (std::is_signed_v<T>) {
        return {.f = {.i = static_cast<std::intmax_t>(n)}, .width = rw, .prec = Field::int_field, .term = term};
    }
    else {
        return {.f = {.u = static_cast<std::uintmax_t>(n)}, .width = rw, .prec = Field::uint_field, .term = term};
    }
}
template <int W = 0, std::integral T>
constexpr auto num(T n, char term = 0) -> Field {
    return num(n, Field::Width{W}, term);
}
template <int W = 0, int P = -1, std::floating_point T>
constexpr auto num(T n, char term = 0) -> Field {
    return {.f = {.d = static_cast<double>(n)}, .width = W, .prec = std::max(P, -1), .term = term};
}
constexpr auto str(std::string_view s, Field::Width w) -> Field {
    return {.f = {.s = s}, .width = to_underlying(w), .prec = Field::str_field, .term = 0};
}
template <int W = 0>
constexpr auto str(std::string_view s) -> Field {
    return str(s, Field::Width{W});
}
///////////////////////////////////////////////////////////////////////////////
// TextStyle
///////////////////////////////////////////////////////////////////////////////
//! Basic (ansi terminal) text style for console output.
/*!
 * \see Potassco::enableAnsiColorSupport()
 */
class TextStyle {
public:
    //! Text emphasis.
    enum class Emphasis : uint8_t { none = 0, bold = 1, faint = 2, italic = 3, underline = 4 };
    //! Terminal colors (only most portable 16 colors + default).
    enum class Color : uint8_t {
        black = 1,
        red,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        white,
        bright_black,
        bright_red,
        bright_green,
        bright_yellow,
        bright_blue,
        bright_magenta,
        bright_cyan,
        bright_white,
        def = 39
    };
    //! Background color specifier.
    struct Bg {
        constexpr explicit Bg(Color bg) : c(bg) {}
        Color c;
    };
    [[nodiscard]] static constexpr auto bg(Color c) -> Bg { return Bg(c); }
    //! Specification of a complete text style consisting of emphasis, foreground and background color.
    struct Spec {
        static Spec fromString(std::string_view str, std::string_view::size_type startPos = 0);
        constexpr Spec() = default;
        constexpr void set(Emphasis e) { em = e; }
        constexpr void set(Color c) { fg = c; }
        constexpr void set(Bg c) { bg = c.c; }
        Emphasis       em{};
        Color          fg{};
        Color          bg{};
    };
    template <typename X>
    static constexpr bool is_style_type =
        std::is_same_v<X, Emphasis> || std::is_same_v<X, Color> || std::is_same_v<X, Bg>;

    //! Convenience operator for building a text style.
    template <typename X>
    requires(is_style_type<X>)
    friend constexpr auto operator|(Spec s, X x) -> Spec {
        s.set(x);
        return s;
    }
    template <typename L, typename R>
    requires(is_style_type<L> && is_style_type<R>)
    friend constexpr auto operator|(L l, R r) -> Spec {
        return Spec{} | l | r;
    }

    //! Creates an empty TextStyle.
    constexpr TextStyle() = default;
    template <typename X>
    requires(is_style_type<X>)
    constexpr TextStyle(X x) : TextStyle(Spec{} | x) {} // NOLINT(*-explicit-constructor)
    constexpr TextStyle(Spec spec) {                    // NOLINT(*-explicit-constructor)
        auto n    = 0u;
        rep_[n++] = '\033';
        rep_[n++] = '[';
        rep_[n++] = static_cast<char>('0' + (static_cast<uint32_t>(spec.em) & 0xFF));
        if (auto cr = static_cast<uint32_t>(spec.fg); cr) {
            cr        += offset(spec.fg);
            rep_[n++]  = ';';
            rep_[n++]  = static_cast<char>('0' + (cr / 10u));
            rep_[n++]  = static_cast<char>('0' + (cr % 10u));
        }
        if (auto cr = static_cast<uint32_t>(spec.bg); cr) {
            cr        += offset(spec.bg) + 10u;
            rep_[n++]  = ';';
            if (cr > 99u) {
                rep_[n++]  = '1';
                cr        %= 100u;
            }
            rep_[n++] = static_cast<char>('0' + (cr / 10u));
            rep_[n++] = static_cast<char>('0' + (cr % 10u));
        }
        rep_[n++]     = 'm';
        rep_[n]       = 0;
        rep_[max_len] = static_cast<char>(max_len - n);
    }
    //! Creates a TextStyle from a string representation in simplified ansi notation (e.g. "1;31" for bold red).
    static TextStyle fromString(std::string_view str, std::string_view::size_type startPos = 0);

    //! Returns the ANSI escape sequence for this text style.
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        if (rep_[0] == 0) {
            return {};
        }
        return {rep_, max_len - static_cast<std::size_t>(rep_[max_len])};
    }
    //! Returns the ANSI escape sequence for this text style.
    [[nodiscard]] constexpr auto c_str() const -> const char* { return rep_; }
    //! Returns the ANSI reset style escape sequence for this text style.
    [[nodiscard]] constexpr auto resetStr() const -> const char* { return rep_[0] != 0 ? ts_reset : ""; }
    //! Returns the ANSI reset style escape sequence for this text style.
    [[nodiscard]] constexpr auto resetView() const -> std::string_view {
        return rep_[0] != 0 ? ts_reset_v : std::string_view{};
    }

    friend constexpr auto operator==(TextStyle a, TextStyle b) -> bool = default;

private:
    friend auto           Detail::resetStyle() -> std::string_view;
    static constexpr auto max_len    = 11u;
    static constexpr auto ts_reset   = "\033[0m";
    static constexpr auto ts_reset_v = std::string_view(ts_reset);
    static constexpr auto offset(Color c) -> uint32_t {
        return static_cast<uint32_t>((c <= Color::white ? 29 : 81) * (c != Color::def));
    }
    char rep_[max_len + 1]{};
};
template <CharBuffer S>
S& toChars(S& out, TextStyle st) {
    return out.append(st.view());
}
template <typename T>
auto styled(const T& arg, const TextStyle& style) -> Augmented<std::remove_cvref_t<T>, Detail::AugmentStyle::styled> {
    return {style.view(), arg};
}
///////////////////////////////////////////////////////////////////////////////
// T -> string
///////////////////////////////////////////////////////////////////////////////
template <auto StackSize = 256>
requires(StackSize > sizeof(DynamicBuffer) + 2)
class BasicCharBufferT { // NOLINT(*-pro-type-member-init)
public:
    static constexpr int eof = -1;

    BasicCharBufferT()                        = default; // NOLINT
    BasicCharBufferT(const BasicCharBufferT&) = default;
    ~BasicCharBufferT()                       = default;
    BasicCharBufferT(BasicCharBufferT&& other) noexcept : BasicCharBufferT() { *this = std::move(other); }
    BasicCharBufferT& operator=(const BasicCharBufferT&) = default;
    BasicCharBufferT& operator=(BasicCharBufferT&& other) noexcept {
        if (this != &other) {
            clear();
            term_ = std::exchange(other.term_, 0);
            ts_   = std::exchange(other.ts_, 0);
            if (other.buffer_.data() == other.local_) {
                append(other.view());
                other.buffer_.clear();
            }
            else {
                buffer_ = std::exchange(other.buffer_, DynamicBuffer(other.local_));
            }
        }
        return *this;
    }

    [[nodiscard]] auto view() const noexcept -> std::string_view { return buffer_.view(); }
    [[nodiscard]] auto size() const noexcept -> uint32_t { return buffer_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return buffer_.size() == 0; }
    [[nodiscard]] auto data() const noexcept -> const char* { return buffer_.data(); }
    [[nodiscard]] auto c_str() -> const char* {
        if (empty() || back() != 0) {
            push_back(0);
        }
        return buffer_.data();
    }
    //! Appends the given arguments according to `fmt`.
    /*!
     * \note Formatting follows the rules of std::vsnprintf().
     */
    BasicCharBufferT& appendF(const char* fmt, ...) noexcept POTASSCO_ATTRIBUTE_FORMAT(2, 3) {
        va_list args;
        va_start(args, fmt);
        Detail::vFormatTo(buffer_, fmt, args);
        va_end(args);
        return *this;
    }
    //! Appends the given arguments according to `fmt`.
    /*!
     * \note Formatting follows the rules of std::vsnprintf().
     */
    BasicCharBufferT& vAppendF(const char* fmt, va_list args) noexcept POTASSCO_ATTRIBUTE_FORMAT(2, 0) {
        Detail::vFormatTo(buffer_, fmt, args);
        return *this;
    }
    //! Appends `s` to the buffer.
    BasicCharBufferT& append(std::string_view s) {
        buffer_.append(s);
        return *this;
    }
    //! Appends `n` copies of `c` to the buffer.
    BasicCharBufferT& append(std::size_t n, char c) {
        std::fill_n(buffer_.alloc(n).data(), n, c);
        return *this;
    }
    //! Appends the given field to the buffer.
    BasicCharBufferT& append(const Field& f) {
        Detail::writeField(buffer_, f);
        return *this;
    }
    //! Converts the given argument and appends its character representation to the buffer.
    template <typename T>
    BasicCharBufferT& append(const T& x) {
        if constexpr (requires { buffer_.append(x); }) {
            buffer_.append(x);
        }
        else {
            static_assert(requires { toChars(*this, x); }, "toChars not defined for type T");
            toChars(*this, x);
        }
        return *this;
    }
    //! Converts and appends the given arguments to the buffer, separated by `sep`.
    template <typename... Args>
    BasicCharBufferT& appendSep(std::string_view sep, const Args&... args) {
        std::string_view seps[2] = {std::string_view{}, sep};
        int              n       = 0;
        (append(seps[Detail::hasValue(args) && n++]).append(args), ...);
        return *this;
    }
    //! Clears the buffer.
    void clear() noexcept {
        term_ = 0;
        ts_   = 0;
        buffer_.clear();
    }
    //! Returns the last character in the buffer.
    /*!
     * \pre not empty()
     */
    char& back() { return buffer_.back(); }
    //! Appends the given character to the buffer.
    void push_back(char c) { buffer_.push(c); }
    //! Pops the last `n` characters from the buffer.
    void pop(uint32_t n) { buffer_.pop(n); }
    //! Opens the buffer for styled output.
    BasicCharBufferT& open(const TextStyle& style, int term = eof) {
        if (ts_) {
            close();
        }
        if (style != TextStyle()) {
            store_set_bit(ts_, 1);
            buffer_.append(style.view());
        }
        if (term != eof) {
            store_set_bit(ts_, 0);
            term_ = static_cast<char>(term);
        }
        return *this;
    }
    //! Finishes styled output and appends the terminator character given on open().
    auto close() -> std::string_view {
        if (test_bit(ts_, 1)) {
            buffer_.append(Detail::resetStyle());
        }
        if (test_bit(ts_, 0)) {
            buffer_.push(term_);
        }
        ts_ = 0;
        return view();
    }

private:
    char          local_[StackSize - (sizeof(DynamicBuffer) + 2)]{};
    char          term_{0};
    uint8_t       ts_{0};
    DynamicBuffer buffer_{local_};
};
using BasicCharBuffer = BasicCharBufferT<>;
static_assert(CharBuffer<BasicCharBuffer> && sizeof(BasicCharBuffer) == 256);

template <typename T, typename... Args>
std::string toString(const T& t, const Args&... args) {
    std::string res;
    std::ignore = toChars(res, t), (toChars(res.append(1, ','), args), ...);
    return res;
}

} // namespace Potassco
