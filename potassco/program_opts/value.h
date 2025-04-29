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

#include <cassert>
#include <cstdint>
#include <string_view>

namespace Potassco::ProgramOptions {

enum DescriptionLevel {
    desc_level_default = 0, //!< Always shown in description.
    desc_level_e1      = 1,
    desc_level_e2      = 2,
    desc_level_e3      = 3,
    desc_level_all     = 4,
    desc_level_hidden  = 5 //!< Never shown in description.
};

class Str {
public:
    template <auto N>
    consteval Str(const char (&s)[N]) : str_{s}
                                      , lit_{true} {}
    Str(const volatile char* s) : str_{const_cast<const char*>(s)} { assert(str_); }
    [[nodiscard]] const char* c_str() const { return str_; }
    [[nodiscard]] bool        isLit() const { return lit_; }
    [[nodiscard]] bool        empty() const { return *str_ == 0; }

    static const char* clone(Str str);

private:
    const char* str_{};
    bool        lit_{false};
};

//! Manages the value of an option and defines how it is parsed from a string.
/*!
 * The library maintains a 1:1-relationship between options and their values.
 * That is, an option has exactly one value and a value has exactly one
 * state w.r.t its option.
 */
class Value {
public:
    //! Possible (tentative) states of an option value.
    enum State {
        value_unassigned = 0, //!< no value assigned
        value_defaulted  = 1, //!< a default value is assigned
        value_fixed      = 2, //!< a parsed value is assigned
    };
    //! Possible value descriptions.
    enum DescType {
        desc_name     = 0,
        desc_default  = 1,
        desc_implicit = 2,
    };
    virtual ~Value();

    void               rtDesc(bool x) { rtDesc_ = x; }
    [[nodiscard]] bool rtDesc() const { return rtDesc_; }

    //! Returns the current state of this value.
    [[nodiscard]] State state() const { return static_cast<State>(state_); }

    /*!
     * Sets the (initial) state of this value to s.
     */
    Value* state(State s) {
        state(true, s);
        return this;
    }

    //! Returns the name of this value.
    /*!
     * \note The default name is "<arg>" unless isFlag() is true, in which
     *       case the default is "".
     */
    [[nodiscard]] const char* arg() const;
    Value*                    arg(Str n) { return desc(desc_name, n); }

    //! Sets an alias name for the corresponding option.
    Value* alias(char c) {
        optAlias_ = static_cast<uint8_t>(c);
        return this;
    }
    [[nodiscard]] char alias() const { return static_cast<char>(optAlias_); }

    //! Sets a description level for the corresponding option.
    /*!
     * Description levels can be used to suppress
     * certain options when generating option descriptions.
     */
    Value* level(DescriptionLevel lev) {
        level_ = static_cast<uint8_t>(lev);
        return this;
    }
    //! Returns the description level of the corresponding option.
    [[nodiscard]] DescriptionLevel level() const { return static_cast<DescriptionLevel>(level_); }

    //! Returns true if this is the value of a negatable option.
    /*!
     * If an option `--option` is negatable, passing `--no-option`
     * on the command-line will set the value of `--option` to `no`.
     */
    [[nodiscard]] bool isNegatable() const { return negatable_ != 0; }
    Value*             negatable() {
        negatable_ = 1;
        return this;
    }

    //! Returns true if the value can be implicitly created from an empty string.
    /*!
     * \note The implicit value comes into play if the corresponding
     *       option is present but without an adjacent value.
     *
     * \note An explicit value for an implicit value is only used if
     *       it is unambiguously given. E.g., on the command-line one has
     *       to use `--option=value` or `-ovalue` but *not* `--option value`
     *       or `-o value`.
     */
    [[nodiscard]] bool isImplicit() const { return implicit_ != 0; }

    //! Returns true if this is the value of an option flag.
    /*!
     * Similar to isImplicit but with the difference that
     * no value is accepted on the command-line.
     *
     * Used for options like `--help` or `--version`.
     */
    [[nodiscard]] bool isFlag() const { return flag_ != 0; }

    /*!
     * Marks the value as a flag.
     * \see bool Value::isFlag() const
     */
    Value* flag() {
        implicit_ = 1;
        flag_     = 1;
        return this;
    }

    //! Returns true if the value of this option can be composed of multiple sources.
    [[nodiscard]] bool isComposing() const { return composing_ != 0; }
    /*!
     * Marks the value as composing.
     * \see Value::isComposing()
     */
    Value* composing() {
        composing_ = 1;
        return this;
    }

    /*!
     * Sets a default value for this value.
     */
    Value* defaultsTo(Str s) { return desc(desc_default, s); }

    //! Returns the default value of this or 0 if none exists.
    [[nodiscard]] const char* defaultsTo() const { return desc(desc_default); }
    /*!
     * Sets an implicit value, which will be used
     * if the option is given without an adjacent value,
     * e.g. `--option` instead of `--option value`
     * \see bool Value::isImplicit() const
     */
    Value* implicit(Str str) { return desc(desc_implicit, str); }
    //! Returns the implicit value of this or 0 if isImplicit() == false.
    [[nodiscard]] const char* implicit() const;

    //! Parses the given string and updates the value's state.
    /*!
     * \param name  The name of the option associated with this value.
     * \param value The value to parse.
     * \param st    The state to which the value should transition if parsing is successful.
     *
     * \return
     * - true if the given string contains a valid value
     * - false otherwise
     *
     * \post if true is returned, state() is st
     */
    bool parse(std::string_view name, std::string_view value, State st = value_fixed);

protected:
    explicit Value(State initial = value_unassigned);
    [[nodiscard]] const char* desc(DescType t) const;
    void                      copyOrBorrow(const char** target, Str source, DescType t);

    bool state(bool b, State s) {
        if (b) {
            state_ = static_cast<uint8_t>(s);
        }
        return b;
    }
    Value* desc(DescType t, Str str);

    virtual bool doParse(std::string_view name, std::string_view value) = 0;

private:
    static constexpr auto desc_pack = 3u;
    union ValueDesc {       // optional value descriptions either
        const char*  value; // a single value or
        const char** pack;  // a pointer to a full pack
    } desc_{nullptr};
    uint8_t state_{0};          // state: one of State
    uint8_t optAlias_{0};       // alias name of option
    uint8_t descVal_   : 2 {0}; // either desc_pack or one of DescType
    uint8_t own_       : 3 {0}; // type of strings we own
    uint8_t rtDesc_    : 1 {0};
    uint8_t reserved_  : 2 {0};
    uint8_t implicit_  : 1 {0}; // implicit value?
    uint8_t flag_      : 1 {0}; // implicit and type bool?
    uint8_t composing_ : 1 {0}; // multiple values allowed?
    uint8_t negatable_ : 1 {0}; // negatable form allowed?
    uint8_t level_     : 4 {0}; // help level
};
} // namespace Potassco::ProgramOptions
