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
#include <potassco/program_opts/intrusive_ptr.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
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

//! Helper class for distinguishing between (static) string literals and possibly temporary strings that must be copied.
class Str {
public:
    constexpr Str() = default;
    template <auto N>
    consteval Str(const char (&s)[N]) requires(N > 0)
        : str_{s}
        , fsz_{((N - 1) << 1) | 1} {
        if (s[N - 1] != 0) {
            fsz_ = (N << 1);
        }
    }
    Str(std::string_view s) : str_{s.data()}, fsz_(s.size() << 1) {}
    Str(const std::string& s) : Str(std::string_view{s}) {}
    Str(const volatile char* s) : Str(std::string_view{const_cast<const char*>(s)}) {}

    [[nodiscard]] constexpr auto str() const -> std::string_view { return {str_, size()}; }
    [[nodiscard]] constexpr auto isLit() const -> bool { return (fsz_ & 1) == 1; }
    [[nodiscard]] constexpr auto empty() const -> bool { return size() == 0; }
    [[nodiscard]] constexpr auto size() const -> std::size_t { return fsz_ >> 1; }

    void removePrefix(std::size_t n) {
        str_  = str_ + n;
        fsz_ -= (n * 2);
    }

private:
    const char* str_{""};
    std::size_t fsz_{1};
};

class Option;

//! Interface for defining a parse action to run when a value for an option is found.
class ValueAction {
public:
    struct Release {
        void operator()(ValueAction* ptr) const noexcept {
            if (ptr->release()) {
                std::default_delete<ValueAction>{}(ptr);
            }
        }
    };
    constexpr ValueAction() = default;
    virtual ~ValueAction();
    //! Parses and assigns the given value to the option.
    /*!
     * \param opt   The option for which the given value was found.
     * \param value The value to parse and assign.
     *
     * \return Whether the given string contains a valid value.
     */
    virtual bool assign(const Option& opt, std::string_view value) = 0;
    //! Returns whether the object can be released.
    virtual bool release() { return true; }
};
using ValueActionPtr = std::unique_ptr<ValueAction, ValueAction::Release>;
template <std::derived_from<ValueAction> T, typename... Args>
auto makeAction(Args&&... args) {
    return std::unique_ptr<T, ValueAction::Release>(new T(std::forward<Args>(args)...));
}

//! Builder class for describing the value of an option.
class ValueDesc {
public:
    constexpr ValueDesc() = default;
    explicit ValueDesc(ValueActionPtr action, uint32_t id = 0) : action_(std::move(action)), id_(id) {}
    template <std::derived_from<ValueAction> T>
    explicit ValueDesc(const IntrusiveSharedPtr<T>& action, uint32_t id = 0) : action_(action.get())
                                                                             , id_(id) {
        if (action) {
            intrusiveAddRef(action.get());
        }
    }
    //! Sets an argument name for the option.
    /*
     * \note In the context of an option, the default name is "<arg>" or "" if `flag()` is set.
     */
    constexpr ValueDesc& arg(Str n) & {
        arg_ = n;
        return *this;
    }
    //! Sets an implicit value, which will be used if the option is given without an adjacent value.
    /*!
     * \note The implicit value comes into play if the corresponding option is present but without an adjacent value.
     * \note An explicit value for an implicit value is only used if it is unambiguously given.
     *       E.g., on the command-line one has to use `--option=value` or `-ovalue` but \b not
     *       `--option value` or `-o value`.
     */
    constexpr ValueDesc& implicit(Str str) & {
        imp_      = str;
        implicit_ = 1;
        return *this;
    }
    //! Marks the option as a flag, i.e., a boolean option that does not take a value.
    /*!
     * Similar to implicit() but with the difference that no value is accepted on the command-line.
     *
     * Used for options like `--help` or `--version`.
     */
    constexpr ValueDesc& flag() & {
        flag_     = 1;
        implicit_ = 1;
        return *this;
    }
    //! Marks the option as negatable.
    /*!
     * If an option `--option` is negatable, passing `--no-option` on the command-line will assign the value
     * `no` to the option (i.e., `--no-option` is treated as `--option=no`).
     */
    constexpr ValueDesc& negatable() & {
        negatable_ = 1;
        return *this;
    }
    //! Marks the option as composing, i.e., acceptíng multiple values (from one or multiple sources).
    constexpr ValueDesc& composing() & {
        composing_ = 1;
        return *this;
    }
    //! Sets a default value for the option.
    /*!
     * If `hasDefault` is true, the option is assumed to initially have its default value.
     */
    constexpr ValueDesc& defaultsTo(Str s, bool hasDefault = false) & {
        def_       = s;
        defaulted_ = hasDefault;
        return *this;
    }
    //! Sets a description level for the option.
    /*!
     * Description levels can be used to suppress certain options when generating
     * option descriptions.
     */
    constexpr ValueDesc& level(DescriptionLevel lev) & {
        level_ = static_cast<unsigned>(lev);
        return *this;
    }

    /*!
     * R-Value overloads
     */
    //@{
    constexpr ValueDesc&& arg(Str n) && { return std::move(arg(n)); }
    constexpr ValueDesc&& implicit(Str str) && { return std::move(implicit(str)); }
    constexpr ValueDesc&& flag() && { return std::move(flag()); }
    constexpr ValueDesc&& negatable() && { return std::move(negatable()); }
    constexpr ValueDesc&& composing() && { return std::move(composing()); }
    constexpr ValueDesc&& defaultsTo(Str s, bool hasDefault = false) && { return std::move(defaultsTo(s, hasDefault)); }
    constexpr ValueDesc&& level(DescriptionLevel lev) && { return std::move(level(lev)); }
    //@}

    /*!
     * Inspection
     */
    //@{
    [[nodiscard]] constexpr auto argName() const -> Str { return arg_; }
    [[nodiscard]] constexpr auto implicitValue() const -> Str { return imp_; }
    [[nodiscard]] constexpr auto defaultValue() const -> Str { return def_; }
    [[nodiscard]] constexpr auto level() const -> DescriptionLevel { return static_cast<DescriptionLevel>(level_); }
    [[nodiscard]] constexpr auto action() const -> const ValueActionPtr& { return action_; }
    [[nodiscard]] constexpr auto action() -> ValueActionPtr& { return action_; }
    [[nodiscard]] constexpr auto id() const -> uint32_t { return id_; }
    [[nodiscard]] constexpr auto isNegatable() const -> bool { return negatable_; }
    [[nodiscard]] constexpr auto isFlag() const -> bool { return flag_ != 0; }
    [[nodiscard]] constexpr auto isComposing() const -> bool { return composing_ != 0; }
    [[nodiscard]] constexpr auto isImplicit() const -> bool { return implicit_ != 0; }
    [[nodiscard]] constexpr auto isDefaulted() const -> bool { return defaulted_; }
    //@}

private:
    ValueActionPtr action_;            // action to execute when a value for the option is found
    Str            arg_{""};           // optional argument name
    Str            def_{""};           // optional default value
    Str            imp_{""};           // optional implicit value
    uint32_t       id_{0};             // numeric id of this value
    uint8_t        level_     : 3 {0}; // help level
    uint8_t        implicit_  : 1 {0}; // implicit value?
    uint8_t        flag_      : 1 {0}; // implicit and type bool?
    uint8_t        composing_ : 1 {0}; // multiple values allowed?
    uint8_t        negatable_ : 1 {0}; // negatable form allowed?
    uint8_t        defaulted_ : 1 {0}; // has its default value assigned?
};

template <typename ActionT>
requires(std::is_constructible_v<ValueDesc, ActionT, uint32_t>)
auto value(ActionT&& action, uint32_t id = 0) -> ValueDesc {
    return ValueDesc{std::forward<ActionT>(action), id};
}

template <typename ActionT, typename... ActionArgs>
auto value(ActionArgs&&... args) -> ValueDesc {
    return ValueDesc{makeAction<ActionT>(std::forward<ActionArgs>(args)...), 0};
}

} // namespace Potassco::ProgramOptions
