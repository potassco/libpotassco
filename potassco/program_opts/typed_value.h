//
// Copyright (c) 2004 - present, Benjamin Kaufmann
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
//
// NOTE: ProgramOptions is inspired by Boost.Program_options
//       see: www.boost.org/libs/program_options
//
#ifndef PROGRAM_OPTIONS_TYPED_VALUE_H_INCLUDED
#define PROGRAM_OPTIONS_TYPED_VALUE_H_INCLUDED
#ifdef _MSC_VER
#pragma warning(disable : 4786)
#pragma warning(disable : 4503)
#pragma warning(disable : 4200)
#endif
#include <potassco/program_opts/errors.h>
#include <potassco/program_opts/value.h>
#include <potassco/string_convert.h>

namespace Potassco::ProgramOptions {
namespace detail {
template <typename T>
struct Parser {
    typedef bool (*type)(const std::string&, T&);
};
} // end namespace detail
///////////////////////////////////////////////////////////////////////////////
// Enumeration Parser: string->int mapping
///////////////////////////////////////////////////////////////////////////////
template <typename EnumT>
auto values(std::vector<std::pair<std::string, EnumT>> vec) {
    return vec;
}

template <typename EnumT, typename OutT>
bool parseValue(const std::vector<std::pair<std::string, EnumT>>& candidates, const std::string& in, OutT& out) {
    for (const auto& [key, value] : candidates) {
        if (strcasecmp(key.c_str(), in.c_str()) == 0) {
            out = static_cast<OutT>(value);
            return true;
        }
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////
// TypedValue
///////////////////////////////////////////////////////////////////////////////
template <typename Callable>
class TypedValue : public Value {
public:
    TypedValue(Callable func) : func_(std::move(func)) {}
    bool doParse(const std::string& opt, const std::string& value) override { return func_(opt, value); }

private:
    Callable func_;
};
///////////////////////////////////////////////////////////////////////////////
// value factories
///////////////////////////////////////////////////////////////////////////////
inline bool store_true(const std::string& v, bool& b) {
    if (bool temp = v.empty(); temp || string_cast<bool>(v, temp)) {
        b = temp;
        return true;
    }
    return false;
}

inline bool store_false(const std::string& v, bool& b) {
    if (bool temp; store_true(v, temp)) {
        b = !temp;
        return true;
    }
    return false;
}

/*!
 * Creates a value that is bound to an existing variable.
 * Assignments to the created value are directly stored in the
 * given variable.
 *
 * \param v The variable to which the new value object is bound.
 * \param p The parser to use for parsing the value. If no parser is given,
 *           type T must provide an operator>>(std::istream&, T&).
 */
template <typename T>
inline Value* storeTo(T& v, typename detail::Parser<T>::type p = &string_cast<T>) {
    return new TypedValue{
        [address = &v, parser = p](const std::string&, const std::string& value) { return parser(value, *address); }};
}

template <typename T, typename U = T>
inline Value* storeTo(T& v, std::vector<std::pair<std::string, U>> values) {
    return new TypedValue{[address = &v, values = std::move(values)](const std::string&, const std::string& value) {
        return parseValue(values, value, *address);
    }};
}

inline Value* flag(bool& b, detail::Parser<bool>::type x = store_true) { return storeTo(b, x)->flag(); }

/*!
 * Creates an action value, i.e. a value for which an action function is called once it was parsed.
 *
 * \param action A callable to be invoked once a value has been parsed.
 * \param parser The parser to use for parsing the value.
 *
 * \see OptionGroup::addOptions()
 */
template <typename T, typename C>
inline Value* action(C&& action, typename detail::Parser<T>::type parser = &string_cast<T>) {
    return new TypedValue{
        [act = std::forward<C>(action), parser = parser](const std::string& name, const std::string& value) {
            T val;
            if (!parser(value, val))
                return false;
            if constexpr (std::is_invocable_v<C, std::string, T>)
                act(name, val);
            else
                act(val);
            return true;
        }};
}

template <typename C>
inline Value* flag(C&& c, detail::Parser<bool>::type x = store_true) {
    return action<bool>(std::forward<C>(c), x)->flag();
}

/*!
 * Creates a custom value, i.e. a value that is fully controlled by the caller.
 *
 * During parsing of options, the given callable is called with its option name and the parsed value.
 * The return value of the callable determines whether the value is considered valid (true) or invalid (false).
 *
 * \param parser A callable to be invoked once a value is parsed.
 *
 * \see OptionGroup::addOptions()
 */
template <typename C>
inline Value* parse(C&& parser) {
    return new TypedValue{[parser = std::forward<C>(parser)](const std::string& name, const std::string& value) {
        if constexpr (std::is_invocable_v<C, std::string, std::string>)
            return parser(name, value);
        else
            return parser(value);
    }};
}

} // namespace Potassco::ProgramOptions

#endif
