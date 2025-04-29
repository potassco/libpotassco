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

#include <potassco/program_opts/string_convert.h>
#include <potassco/program_opts/value.h>

#include <vector>

namespace Potassco::ProgramOptions {
template <typename C, typename T>
concept ActionFunction = std::is_invocable_v<C, std::string_view, T> || std::is_invocable_v<C, T>;

//! Parser adapter for Potassco::stringTo.
struct DefaultParser {
    template <typename T>
    bool operator()(std::string_view v, T& out) const {
        return Parse::ok(Potassco::stringTo(v, out));
    }
};
//! Parser adapter for parsing a fixed set of values.
template <typename V>
struct ParseValues {
    explicit ParseValues(std::vector<std::pair<std::string, V>> candidates) : values(std::move(candidates)) {}
    template <typename T>
    bool operator()(std::string_view in, T& out) const {
        for (const auto& [key, value] : values) {
            if (Parse::eqIgnoreCase(key, in)) {
                out = static_cast<T>(value);
                return true;
            }
        }
        return false;
    }
    std::vector<std::pair<std::string, V>> values;
};

//! Value type for storing a parsed value directly into a given variable.
template <typename T, typename P = DefaultParser>
class Store : public Value {
public:
    Store(T& v, P p) : address_(&v), parser_(std::move(p)) {}
    bool doParse(std::string_view, std::string_view value) override { return parser_(value, *address_); }

private:
    T*                                address_;
    POTASSCO_ATTR_NO_UNIQUE_ADDRESS P parser_{};
};

//! Value type for calling a user-provided action function.
template <typename T, ActionFunction<T> C, typename P = DefaultParser>
class Action : public Value {
public:
    Action(C callable, P p) : action_(std::move(callable)), parser_(std::move(p)) {}
    bool doParse([[maybe_unused]] std::string_view opt, std::string_view value) override {
        T out;
        if (not parser_(value, out)) {
            return false;
        }
        if constexpr (std::is_invocable_v<C, std::string_view, T>) {
            action_(opt, out);
        }
        else {
            action_(out);
        }
        return true;
    }

private:
    C                                 action_;
    POTASSCO_ATTR_NO_UNIQUE_ADDRESS P parser_{};
};

//! Value type for calling a user-provided option handler.
template <typename Callable>
class Custom : public Value {
public:
    explicit Custom(Callable func) : handler_(std::move(func)) {}
    bool doParse([[maybe_unused]] std::string_view opt, std::string_view value) override {
        if constexpr (std::is_invocable_v<Callable, std::string_view, std::string_view>) {
            return handler_(opt, value);
        }
        else {
            return handler_(value);
        }
    }

private:
    Callable handler_;
};
///////////////////////////////////////////////////////////////////////////////
// value factories
///////////////////////////////////////////////////////////////////////////////
//! Returns a parser for mapping a string to some EnumT.
template <typename EnumT>
auto values(std::vector<std::pair<std::string, EnumT>> vec) -> ParseValues<EnumT> {
    return ParseValues<EnumT>{std::move(vec)};
}
/*!
 * Creates a value bound to the given variable.
 * Assignments to the created value are directly stored in the given variable.
 *
 * \param v The variable to which the new value object is bound.
 * \param parser The (optional) parser to use for converting from string to T.
 */
template <typename T, typename P = DefaultParser>
Value* storeTo(T& v, P&& parser = P()) {
    return new Store{v, std::forward<P>(parser)};
}

template <typename T>
Value* storeTo(T& v, T init) {
    return storeTo(v = std::move(init));
}

/*!
 * Creates an action value, i.e., a value for which an action function is called once it was parsed.
 *
 * \param action A callable to be invoked once a value of `T` has been parsed.
 * \param parser The parser to use for parsing the value.
 *
 * \see OptionGroup::addOptions()
 */
template <typename T, ActionFunction<T> C, typename P = DefaultParser>
Value* action(C&& action, P&& parser = P()) {
    return new Action<T, C, P>{std::forward<C>(action), std::forward<P>(parser)};
}

//! Alternative flag action.
constexpr inline auto store_false = [](std::string_view v, bool& b) {
    if (bool temp; DefaultParser{}(v, temp)) {
        b = not temp;
        return true;
    }
    return false;
};

//! Creates a flag value with an optional action function.
/*!
 * \param b A boolean variable that should receive parsed values or a callable to invoke when a value is parsed.
 * \param pa An optional parser for converting a string into a boolean value.
 */
template <typename F, typename P = DefaultParser>
Value* flag(F&& b, P&& pa = P()) {
    static_assert(std::is_same_v<F, bool&> or ActionFunction<F, bool>,
                  "'b' must be a modifiable lvalue reference to bool or a callable");
    if constexpr (ActionFunction<F, bool>) {
        return action<bool>(std::forward<F>(b), std::forward<P>(pa))->flag();
    }
    else {
        return storeTo(std::forward<F>(b), std::forward<P>(pa))->flag();
    }
}
template <typename P = DefaultParser>
Value* flag(bool& b, bool init, P&& pa = P()) {
    return storeTo(b = init, std::forward<P>(pa))->flag();
}

/*!
 * Creates a custom value, i.e., a value that is fully controlled by the caller.
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
    return new Custom{std::forward<C>(parser)};
}

} // namespace Potassco::ProgramOptions
