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
// NOTE: ProgramOptions is inspired by Boost.Program_options
//       see: www.boost.org/libs/program_options
//
#pragma once

#include <stdexcept>
#include <string>

namespace Potassco::ProgramOptions {

//! Base class for all exceptions.
class Error : public std::logic_error {
public:
    explicit Error(const std::string& what) : std::logic_error(what) {}
};

//! Used for signaling errors on the command-line and in declaring options.
class SyntaxError : public Error {
public:
    enum Type { missing_value, extra_value, invalid_format };
    SyntaxError(Type t, std::string_view key);
    [[nodiscard]] Type               type() const { return type_; }
    [[nodiscard]] const std::string& key() const { return key_; }

private:
    std::string key_;
    Type        type_;
};

//! Used for signaling errors in OptionContext.
class ContextError : public Error {
public:
    enum Type { duplicate_option, unknown_option, ambiguous_option, unknown_group };
    ContextError(std::string_view ctx, Type t, std::string_view key, std::string_view desc = {});
    [[nodiscard]] Type               type() const { return type_; }
    [[nodiscard]] const std::string& key() const { return key_; }
    [[nodiscard]] const std::string& ctx() const { return ctx_; }

private:
    std::string ctx_;
    std::string key_;
    Type        type_;
};

class DuplicateOption : public ContextError {
public:
    DuplicateOption(std::string_view ctx, std::string_view key) : ContextError(ctx, duplicate_option, key) {}
};
class UnknownOption : public ContextError {
public:
    UnknownOption(std::string_view ctx, std::string_view key) : ContextError(ctx, unknown_option, key) {}
};
class AmbiguousOption : public ContextError {
public:
    AmbiguousOption(std::string_view ctx, std::string_view key, std::string_view alt)
        : ContextError(ctx, ambiguous_option, key, alt) {}
};

//! Used for signaling validation errors when trying to assign option values.
class ValueError : public Error {
public:
    enum Type { multiple_occurrences, invalid_default, invalid_value };
    ValueError(std::string_view ctx, Type t, std::string_view opt, std::string_view value);
    [[nodiscard]] Type               type() const { return type_; }
    [[nodiscard]] const std::string& key() const { return key_; }
    [[nodiscard]] const std::string& ctx() const { return ctx_; }
    [[nodiscard]] const std::string& value() const { return value_; }

private:
    std::string ctx_;
    std::string key_;
    std::string value_;
    Type        type_;
};

} // namespace Potassco::ProgramOptions
