//
// Copyright (c) 2017 - present, Benjamin Kaufmann
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
#ifndef POTASSCO_STRING_CONVERT_H_INCLUDED
#define POTASSCO_STRING_CONVERT_H_INCLUDED
#include <potassco/platform.h>

#include <potassco/basic_types.h>

#include <climits>
#include <cstdarg>
#include <cstring>
#include <istream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
#if !defined(_MSC_VER)
#include <strings.h>
#else
inline int strcasecmp(const char* lhs, const char* rhs) { return _stricmp(lhs, rhs); }
inline int strncasecmp(const char* lhs, const char* rhs, size_t n) { return _strnicmp(lhs, rhs, n); }
#endif
namespace Potassco {
namespace detail {
// A primitive input stream buffer for fast extraction from a given string
// NOTE: The input string is NOT COPIED, hence it
//       MUST NOT CHANGE during extraction
template <typename T, typename Traits = std::char_traits<T>>
class input_from_string : public std::basic_streambuf<T, Traits> {
    using base_type          = std::basic_streambuf<T, Traits>;
    using pointer_type       = typename Traits::char_type*;
    using const_pointer_type = const typename Traits::char_type*;
    using pos_type           = typename base_type::pos_type;
    using off_type           = typename base_type::off_type;

public:
    explicit input_from_string(const_pointer_type p, size_t size) : buffer_(const_cast<pointer_type>(p)), size_(size) {
        base_type::setp(0, 0);                              // no write buffer
        base_type::setg(buffer_, buffer_, buffer_ + size_); // read buffer
    }
    input_from_string(const input_from_string&)            = delete;
    input_from_string& operator=(const input_from_string&) = delete;

    pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode which) override {
        if (which & std::ios_base::out) {
            // not supported!
            return base_type::seekoff(offset, dir, which);
        }
        if (dir == std::ios_base::cur) {
            offset += static_cast<off_type>(base_type::gptr() - base_type::eback());
        }
        else if (dir == std::ios_base::end) {
            offset = static_cast<off_type>(size_) - offset;
        }
        return seekpos(offset, which);
    }
    pos_type seekpos(pos_type offset, std::ios_base::openmode which) override {
        if ((which & std::ios_base::out) == 0 && offset >= pos_type(0) && ((size_t) offset) <= size_) {
            base_type::setg(buffer_, buffer_ + (size_t) offset, buffer_ + size_);
            return offset;
        }
        return base_type::seekpos(offset, which);
    }

protected:
    pointer_type buffer_;
    size_t       size_;
};

template <typename T, typename Traits = std::char_traits<T>>
class input_stream : public std::basic_istream<T, Traits> {
public:
    input_stream(const std::string& str) : std::basic_istream<T, Traits>(0), buffer_(str.data(), str.size()) {
        std::basic_istream<T, Traits>::rdbuf(&buffer_);
    }
    input_stream(const char* x, size_t size) : std::basic_istream<T, Traits>(0), buffer_(x, size) {
        std::basic_istream<T, Traits>::rdbuf(&buffer_);
    }

private:
    input_from_string<T, Traits> buffer_;
};
struct no_stream_support {
    template <typename T>
    no_stream_support(const T&) {}
};
no_stream_support& operator>>(std::istream&, const no_stream_support&);
} // namespace detail
///////////////////////////////////////////////////////////////////////////////
// primitive parser
///////////////////////////////////////////////////////////////////////////////
template <typename T>
int xconvert(const char* x, T& out, const char** errPos = nullptr, double = 0);
int xconvert(const char* x, bool& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, char& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, unsigned& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, int& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, long& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, unsigned long& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, double& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, const char*& out, const char** errPos = nullptr, int = 0);
int xconvert(const char* x, std::string& out, const char** errPos = nullptr, int sep = 0);
template <typename T>
auto xconvert(const char* x, T& out, const char** errPos, int e = 0) -> decltype(int(T::enumClass().convert(x, e))) {
    size_t len = T::enumClass().convert(x, e);
    if (errPos) {
        *errPos = x + len;
    }
    if (len) {
        out = static_cast<T>(e);
    }
    return int(len > 0u);
}
std::string&        xconvert(std::string&, bool);
std::string&        xconvert(std::string&, char);
std::string&        xconvert(std::string&, int);
std::string&        xconvert(std::string&, unsigned int);
std::string&        xconvert(std::string&, long);
std::string&        xconvert(std::string&, unsigned long);
std::string&        xconvert(std::string&, double);
inline std::string& xconvert(std::string& out, const std::string& s) { return out.append(s); }
inline std::string& xconvert(std::string& out, const char* s) { return out.append(s ? s : ""); }
template <typename T>
auto xconvert(std::string& out, T x)
    -> std::enable_if_t<std::is_convertible_v<decltype(T::enumClass()), Potassco::EnumClass>, std::string&> {
    const char* key;
    size_t      len = T::enumClass().convert(static_cast<int>(x), key);
    return out.append(key, len);
}
#if defined(LLONG_MAX)
int          xconvert(const char* x, long long& out, const char** errPos = nullptr, int = 0);
int          xconvert(const char* x, unsigned long long& out, const char** errPos = nullptr, int = 0);
std::string& xconvert(std::string&, long long x);
std::string& xconvert(std::string&, unsigned long long x);
#endif
///////////////////////////////////////////////////////////////////////////////
// composite parser
///////////////////////////////////////////////////////////////////////////////
const int def_sep = int(',');
template <class T>
int xconvert(const char* x, std::vector<T>& out, const char** errPos = nullptr, int sep = def_sep);

// parses T[,U] optionally enclosed in parentheses
template <class T, class U>
int xconvert(const char* x, std::pair<T, U>& out, const char** errPos = nullptr, int sep = def_sep) {
    if (!x) {
        return 0;
    }
    if (sep == 0) {
        sep = def_sep;
    }
    std::pair<T, U> temp(out);
    const char*     n  = x;
    int             ps = 0;
    if (*n == '(') {
        ++ps;
        ++n;
    }
    int tokT = xconvert(n, temp.first, &n, sep);
    int tokU = tokT && *n == (char) sep && n[1] ? xconvert(n + 1, temp.second, &n, sep) : 0;
    int sum  = 0;
    if (!ps || *n == ')') {
        n += ps;
        if (tokU) {
            out.second = temp.second;
            ++sum;
        }
        if (tokU || !*n) {
            out.first = temp.first;
            ++sum;
        }
    }
    if (!sum) {
        n = x;
    }
    if (errPos)
        *errPos = n;
    return sum;
}
// parses T1 [, ..., Tn] optionally enclosed in brackets
template <class T, class OutIt>
std::size_t convert_seq(const char* x, std::size_t maxLen, OutIt out, char sep, const char** errPos = nullptr) {
    if (!x) {
        return 0;
    }
    const char* n = x;
    std::size_t t = 0;
    std::size_t b = 0;
    if (*n == '[') {
        ++b;
        ++n;
    }
    while (t != maxLen) {
        T temp;
        if (!xconvert(n, temp, &n, sep))
            break;
        *out++ = temp;
        ++t;
        if (!*n || *n != (char) sep || !n[1])
            break;
        n = n + 1;
    }
    if (!b || *n == ']') {
        n += b;
    }
    else {
        n = x;
    }
    if (errPos)
        *errPos = n;
    return t;
}
// parses T1 [, ..., Tn] optionally enclosed in brackets
template <class T>
int xconvert(const char* x, std::vector<T>& out, const char** errPos, int sep) {
    if (sep == 0) {
        sep = def_sep;
    }
    std::size_t sz = out.size();
    std::size_t t  = convert_seq<T>(x, out.max_size() - sz, std::back_inserter(out), static_cast<char>(sep), errPos);
    if (!t) {
        out.resize(sz);
    }
    return static_cast<int>(t);
}
template <class T, int sz>
int xconvert(const char* x, T (&out)[sz], const char** errPos = nullptr, int sep = 0) {
    return static_cast<int>(convert_seq<T>(x, sz, out, static_cast<char>(sep ? sep : def_sep), errPos));
}
template <class T, class U>
std::string& xconvert(std::string& out, const std::pair<T, U>& in, char sep = static_cast<char>(def_sep)) {
    xconvert(out, in.first).append(1, sep);
    return xconvert(out, in.second);
}
template <class IT>
std::string& xconvert(std::string& accu, IT begin, IT end, char sep = static_cast<char>(def_sep)) {
    for (bool first = true; begin != end; first = false) {
        if (!first) {
            accu += sep;
        }
        xconvert(accu, *begin++);
    }
    return accu;
}
template <class T>
std::string& xconvert(std::string& out, const std::vector<T>& in, char sep = static_cast<char>(def_sep)) {
    return xconvert(out, in.begin(), in.end(), sep);
}
///////////////////////////////////////////////////////////////////////////////
// fall back parser
///////////////////////////////////////////////////////////////////////////////
template <class T>
int xconvert(const char* x, T& out, const char** errPos, double) {
    std::size_t                xLen = std::strlen(x);
    const char*                err  = x;
    detail::input_stream<char> str(x, xLen);
    if (str >> out) {
        if (str.eof()) {
            err += xLen;
        }
        else {
            err += static_cast<std::size_t>(str.tellg());
        }
    }
    if (errPos) {
        *errPos = err;
    }
    return int(err != x);
}
///////////////////////////////////////////////////////////////////////////////
// string -> T
///////////////////////////////////////////////////////////////////////////////
class bad_string_cast : public std::bad_cast {
public:
    [[nodiscard]] const char* what() const noexcept override;
};
template <class T>
bool string_cast(const char* arg, T& to) {
    const char* end;
    return xconvert(arg, to, &end, 0) != 0 && !*end;
}
template <class T>
T string_cast(const char* s) {
    T to;
    if (string_cast<T>(s, to)) {
        return to;
    }
    throw bad_string_cast();
}
template <class T>
T string_cast(const std::string& s) {
    return string_cast<T>(s.c_str());
}
template <class T>
bool string_cast(const std::string& from, T& to) {
    return string_cast<T>(from.c_str(), to);
}

template <class T>
bool stringTo(const char* str, T& x) {
    return string_cast(str, x);
}
///////////////////////////////////////////////////////////////////////////////
// T -> string
///////////////////////////////////////////////////////////////////////////////
template <class U>
std::string string_cast(const U& num) {
    std::string out;
    xconvert(out, num);
    return out;
}
template <class T>
inline std::string toString(const T& x) {
    return string_cast(x);
}
template <class T, class U>
inline std::string toString(const T& x, const U& y) {
    std::string res;
    xconvert(res, x).append(1, ',');
    return xconvert(res, y);
}
template <class T, class U, class V>
std::string toString(const T& x, const U& y, const V& z) {
    std::string res;
    xconvert(res, x).append(1, ',');
    xconvert(res, y).append(1, ',');
    return xconvert(res, z);
}

#define POTASSCO_FORMAT_S(str, fmt, ...) (Potassco::StringBuilder(str).appendFormat((fmt), __VA_ARGS__), (str))
#define POTASSCO_FORMAT(fmt, ...)        (Potassco::StringBuilder().appendFormat((fmt), __VA_ARGS__).c_str())

int vsnprintf(char* s, size_t n, const char* format, va_list arg);
//! A class for creating a sequence of characters.
class StringBuilder {
public:
    using ThisType = StringBuilder;
    enum Mode { Fixed, Dynamic };
    //! Constructs an empty object with an initial capacity of 63 characters.
    explicit StringBuilder();
    //! Constructs an object that appends new characters to the given string s.
    explicit StringBuilder(std::string& s);
    //! Constrcuts an object that stores up to n characters in the given array.
    /*!
     * If m is Fixed, the maximum size of this sequence is fixed to n.
     * Otherwise, the sequence may switch to an internal buffer once size()
     * exceeds n characters.
     */
    explicit StringBuilder(char* buf, std::size_t n, Mode m = Fixed);
    ~StringBuilder();
    StringBuilder(const StringBuilder&)            = delete;
    StringBuilder& operator=(const StringBuilder&) = delete;

    //! Returns the character sequence as a null-terminated string.
    [[nodiscard]] const char* c_str() const;
    //! Returns the length of this character sequence, i.e. std::strlen(c_str()).
    [[nodiscard]] std::size_t size() const;
    //! Returns if character sequence is empty, i.e. std::strlen(c_str()) == 0.
    [[nodiscard]] bool             empty() const { return size() == std::size_t(0); }
    [[nodiscard]] std::string_view view() const;
    //! Resizes this character sequence to a length of n characters.
    /*!
     * \throw std::logic_error if n > maxSize()
     * \throw std::bad_alloc if the function needs to allocate storage and fails.
     */
    ThisType& resize(std::size_t n, char c = '\0');
    //! Returns the maximum size of this sequence.
    [[nodiscard]] std::size_t maxSize() const;
    //! Clears this character sequence.
    ThisType& clear() { return resize(std::size_t(0)); }

    //! Returns the character at the specified position.
    char  operator[](std::size_t p) const { return buffer().head[p]; }
    char& operator[](std::size_t p) { return buffer().head[p]; }

    //! Appends the given null-terminated string.
    ThisType& append(const char* str);
    //! Appends the first n characters in str.
    ThisType& append(const char* str, std::size_t n);
    //! Appends n consecutive copies of character c.
    ThisType& append(std::size_t n, char c);
    //! Appends the given number.
    ThisType& append(int n) { return append_(static_cast<uint64_t>(static_cast<int64_t>(n)), n >= 0); }
    ThisType& append(long n) { return append_(static_cast<uint64_t>(static_cast<int64_t>(n)), n >= 0); }
    ThisType& append(long long n) { return append_(static_cast<uint64_t>(static_cast<int64_t>(n)), n >= 0); }
    ThisType& append(unsigned int n) { return append_(static_cast<uint64_t>(n), true); }
    ThisType& append(unsigned long n) { return append_(static_cast<uint64_t>(n), true); }
    ThisType& append(unsigned long long n) { return append_(static_cast<uint64_t>(n), true); }
    ThisType& append(float x) { return append(static_cast<double>(x)); }
    ThisType& append(double x);
    //! Appends the null-terminated string fmt, replacing any format specifier in the same way as printf does.
    ThisType& appendFormat(const char* fmt, ...);

private:
    ThisType& append_(uint64_t n, bool pos);
    enum Type { Sbo = 0u, Str = 64u, Buf = 128u };
    static constexpr uint8_t Own    = 1u;
    static constexpr uint8_t SboCap = 63u;
    struct Buffer {
        [[nodiscard]] std::size_t free() const { return size - used; }
        [[nodiscard]] char*       pos() const { return head + used; }
        char*                     head;
        std::size_t               used;
        std::size_t               size;
    };
    [[nodiscard]] uint8_t tag() const { return static_cast<uint8_t>(sbo_[63]); }
    [[nodiscard]] Type    type() const { return static_cast<Type>(tag() & uint8_t(Str | Buf)); }
    [[nodiscard]] Buffer  buffer() const;

    void   setTag(uint8_t t) { reinterpret_cast<uint8_t&>(sbo_[63]) = t; }
    Buffer grow(std::size_t n);

    union {
        std::string* str_;
        Buffer       buf_;
        char         sbo_[64];
    };
};

} // namespace Potassco

#endif
