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
#pragma once

#include <potassco/program_opts/errors.h>
#include <potassco/program_opts/string_convert.h>
#include <potassco/program_opts/value.h>

namespace Potassco::ProgramOptions {
struct Parser {
    template <typename T>
    bool operator()(const std::string& v, T& out) const {
        return Parse::ok(Potassco::stringTo(v, out));
    }
};

template <typename T, typename P, typename A>
bool runAction(const std::string& name, const std::string& value, const P& p, const A& act) {
    T out;
    if (not p(value, out)) {
        return false;
    }
    if constexpr (std::is_invocable_v<A, std::string, T>) {
        act(name, out);
    }
    else {
        act(out);
    }
    return true;
}
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
        if (key.length() == in.length() && Parse::eqIgnoreCase(key.c_str(), in.c_str(), in.length())) {
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
    if (bool temp = v.empty(); temp || Parser{}(v, temp)) {
        b = temp;
        return true;
    }
    return false;
}

inline bool store_false(const std::string& v, bool& b) {
    if (bool temp; store_true(v, temp)) {
        b = not temp;
        return true;
    }
    return false;
}

/*!
 * Creates a value that is bound to an existing variable.
 * Assignments to the created value are directly stored in the given variable.
 *
 * \param v The variable to which the new value object is bound.
 */
template <typename T>
Value* storeTo(T& v) {
    return new TypedValue{
        [address = &v](const std::string&, const std::string& value) { return Parser{}(value, *address); }};
}

template <typename T>
Value* storeTo(T& v, T init) {
    return storeTo(v = std::move(init));
}

/*!
 * Creates a value that is bound to an existing variable.
 * Assignments to the created value are directly stored in the given variable.
 *
 * \param v The variable to which the new value object is bound.
 * \param parser The parser to use for parsing the value.
 */
template <typename T, typename P>
Value* storeTo(T& v, P parser) {
    return new TypedValue{[address = &v, parser = std::move(parser)](const std::string&, const std::string& value) {
        return parser(value, *address);
    }};
}

template <typename T, typename U = T>
Value* storeTo(T& v, std::vector<std::pair<std::string, U>> values) {
    return storeTo(
        v, [values = std::move(values)](const std::string& val, T& out) { return parseValue(values, val, out); });
}

inline Value* flag(bool& b, bool (*action)(const std::string&, bool&) = store_true) {
    return storeTo(b, action)->flag();
}

inline Value* flag(bool& b, bool init, bool (*action)(const std::string&, bool&) = store_true) {
    return flag(b = init, action);
}

/*!
 * Creates an action value, i.e. a value for which an action function is called once it was parsed.
 *
 * \param action A callable to be invoked once a value has been parsed.
 *
 * \see OptionGroup::addOptions()
 */
template <typename T, typename C>
Value* action(C&& action) {
    return new TypedValue{[act = std::forward<C>(action)](const std::string& name, const std::string& value) {
        return runAction<T>(name, value, Parser(), act);
    }};
}

/*!
 * Creates an action value, i.e. a value for which an action function is called once it was parsed.
 *
 * \param action A callable to be invoked once a value has been parsed.
 * \param parser The parser to use for parsing the value.
 *
 * \see OptionGroup::addOptions()
 */
template <typename T, typename C, typename P>
Value* action(C&& action, P parser) {
    return new TypedValue{
        [act = std::forward<C>(action), parser = std::move(parser)](const std::string& name, const std::string& value) {
            return runAction<T>(name, value, parser, act);
        }};
}

template <typename C>
Value* flag(C&& c, bool (*x)(const std::string&, bool&) = store_true) {
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
Value* parse(C&& parser) {
    return new TypedValue{
        [parser = std::forward<C>(parser)]([[maybe_unused]] const std::string& name, const std::string& value) {
            if constexpr (std::is_invocable_v<C, std::string, std::string>) {
                return parser(name, value);
            }
            else {
                return parser(value);
            }
        }};
}

} // namespace Potassco::ProgramOptions
