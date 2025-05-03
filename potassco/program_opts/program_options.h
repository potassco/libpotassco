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
#include <potassco/program_opts/detail/refcountable.h>
#include <potassco/program_opts/value.h>

#include <cstdio>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace Potassco::ProgramOptions {

//! Represents one program option.
/*!
 * An Option consists of a description (long name, short name, description),
 * a (typed) value, and an optional default value.
 *
 * \note
 *   When printing an option, occurrences of %D, %I, and %A in its description are replaced
 *   with the option's default value, implicit value, and the argument name,
 *   respectively.
 */
class Option : public Detail::RefCountable {
public:
    /*!
     * \pre name != ""
     * \pre value != nullptr
     * \param name        name (and unique key) of the option
     * \param description description of the option, used for printing help
     * \param value       value object to be associated with this option
     */
    Option(Str name, Str description, std::unique_ptr<Value> value);
    Option(Str name, Str description, Value* value);
    Option(Str name, char alias, Str description, Value* value);

    ~Option();

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] char             alias() const { return value_->alias(); }
    [[nodiscard]] Value*           value() const { return value_.get(); }
    [[nodiscard]] std::string_view description() const { return description_; }
    [[nodiscard]] std::string_view argName() const { return value_->arg(); }
    [[nodiscard]] bool             assignDefault() const;
    [[nodiscard]] std::size_t      maxColumn() const;
    [[nodiscard]] DescriptionLevel descLevel() const { return value_->level(); }

private:
    const char*            name_;        // name (and unique key) of option
    const char*            description_; // description of the option (used for --help)
    std::unique_ptr<Value> value_;       // the option's value manager
};

using SharedOptPtr = Detail::IntrusiveSharedPtr<Option>;

class OptionInitHelper;
class OptionContext;
class OptionParser;
class ParsedOptions;
class OptionOutput;

//! A list of options logically grouped under a caption.
/*!
 * The class provides a logical grouping of options that
 * is mainly useful for printing help.
 */
class OptionGroup {
public:
    using OptionList      = std::vector<SharedOptPtr>;
    using option_iterator = OptionList::const_iterator; // NOLINT

    /*!
     * Creates a new group of options under the given caption.
     */
    explicit OptionGroup(std::string_view caption = "", DescriptionLevel descLevel = desc_level_default);

    //! Returns the caption of this group.
    [[nodiscard]] const std::string& caption() const { return caption_; }

    [[nodiscard]] std::size_t      size() const { return options_.size(); }
    [[nodiscard]] bool             empty() const { return options_.empty(); }
    [[nodiscard]] option_iterator  begin() const { return options_.begin(); }
    [[nodiscard]] option_iterator  end() const { return options_.end(); }
    [[nodiscard]] DescriptionLevel descLevel() const { return level_; }

    //! Returns an object that can be used to add options.
    /*!
     * \par usage \n
     * \code
     * OptionGroup g("Some Options");
     * int i; double d; char c;
     * g.addOptions()
     *   ("opt1", storeTo(i), "some int value") // <- no semicolon
     *   ("opt2", storeTo(d))                   // <- no semicolon
     *   ("opt3", storeTo(c))                   // <- no semicolon
     * ;                                        // <- note the semicolon!
     * \endcode
     */
    OptionInitHelper addOptions();

    //! Adds the given option to this group.
    void addOption(const SharedOptPtr& option);

    void setDescriptionLevel(DescriptionLevel level) { level_ = level; }

    //! Creates a formatted description of all options with level() <= level in this group.
    void format(OptionOutput& out, size_t maxW, DescriptionLevel level = desc_level_default) const;

    [[nodiscard]] std::size_t maxColumn(DescriptionLevel level) const;

private:
    friend class OptionContext;
    std::string      caption_;
    OptionList       options_;
    DescriptionLevel level_;
};

class OptionInitHelper {
public:
    //! Applies the given spec.
    /*!
     * Parses and applies a spec like `[!][+][*][-<alias>][@<level>]` such that:
     *  - `!` is mapped to `value.negatable()`,
     *  - `+` is mapped to `value.composing()`,
     *  - `*` is mapped to `value.flag()`,
     *  - `-<alias>` is mapped to `value.alias(<alias>)`, and
     *  - `@<level>` is mapped `value.level(enum_cast<DescriptionLevel>(level))`
     *
     *  \return true if `spec` is valid, i.e., elements are either not present or valid.
     */
    static bool applySpec(std::string_view spec, Value& value);

    explicit OptionInitHelper(OptionGroup& owner);

    //! Factory function for creating an option.
    /*!
     * \param name Name (and unique key) of the option
     * \param spec Additional value specification (see OptionInitHelper::applySpec(std::string_view, Value&)
     * \param val  Value of the option
     * \param desc Description of the option
     *
     * \throw Error if `<name>` is empty or `<spec>` is not valid.
     */
    OptionInitHelper& operator()(Str name, std::string_view spec, Value* val, Str desc);

    //! Factory function for creating an option.
    /*!
     * \overload OptionInitHelper::operator()(Str name, std::string_view spec, Value* val, Str desc)
     */
    OptionInitHelper& operator()(Str name, Value* val, Str desc);

private:
    OptionGroup* owner_;
};

//! A (logically grouped) list of unique options.
/*!
 * An option context stores a list of option groups.
 * Options in a context have to be unique (w.r.t name and alias)
 * within that context.
 *
 * An OptionContext defines the granularity of option parsing and option lookup.
 */
class OptionContext {
private:
    using KeyType    = std::size_t;
    using Name2Key   = std::map<std::string, KeyType, std::less<>>;
    using GroupList  = std::vector<OptionGroup>;
    using OptionList = OptionGroup::OptionList;

public:
    //! Type for identifying an option within a context
    using option_iterator = OptionList::const_iterator; // NOLINT

    explicit OptionContext(std::string_view caption = "", DescriptionLevel desc_default = desc_level_default);

    [[nodiscard]] const std::string& caption() const;

    //! Adds the given group of options to this context.
    /*!
     * \note  If this object already contains a group with
     *        the same caption as `group`, the groups are merged.
     *
     * \throw DuplicateOption if an option in `group`
     *        has the same short or long name as one of the
     *        options in this context.
     */
    OptionContext& add(const OptionGroup& group);

    //! Adds an alias name for the option at the given position.
    /*!
     * \throw DuplicateOption if an option with the name aliasName already exists.
     */
    OptionContext& addAlias(std::size_t pos, std::string_view aliasName);

    //! Adds all groups (and their options) from other to this context.
    /*!
     * \throw DuplicateOption if an option in other
     *        has the same short or long name as one of the
     *        options in this context.
     *
     * \see OptionContext& add(const OptionGroup&);
     */
    OptionContext& add(const OptionContext& other);

    [[nodiscard]] option_iterator begin() const { return options_.begin(); }
    [[nodiscard]] option_iterator end() const { return options_.end(); }

    //! Returns the number of options in this context.
    [[nodiscard]] std::size_t size() const { return options_.size(); }
    //! Returns the number of groups in this context
    [[nodiscard]] std::size_t groups() const { return groups_.size(); }

    enum FindType { find_name = 1, find_prefix = 2, find_name_or_prefix = find_name | find_prefix, find_alias = 4 };

    //! Returns the option with the given name or prefix.
    /*!
     * \note The second parameter `t` defines how `name` is interpreted:
     *        - find_name:   search for an option whose name equals `name`
     *        - find_prefix: search for an option whose name starts with `name`
     *        - find_alias:  search for an option whose alias equals `name`
     *
     * \note If `t` is find_alias, a starting '-' in `name` is valid but not required.
     *
     * \throw UnknownOption if no option matches `name`.
     * \throw AmbiguousOption if more than one option matches `name`.
     */
    [[nodiscard]] auto option(std::string_view name, FindType t = find_name) const -> SharedOptPtr;
    [[nodiscard]] auto operator[](std::string_view name) const -> const Option&;

    //! Returns the index of the option with the given name.
    /*!
     * \throw UnknownOption if no option matches `name`.
     */
    [[nodiscard]] auto optionIndex(std::string_view name) const -> std::size_t;

    //! Returns the option group with the given caption or throws `ContextError` if no such group exists.
    [[nodiscard]] const OptionGroup& group(std::string_view caption) const;
    //! Sets the description level to be used when generating a description.
    /*!
     * Once set, functions generating descriptions will only consider groups
     * and options with description level <= std::min(level, desc_level_all).
     */
    void                           setActiveDescLevel(DescriptionLevel level);
    [[nodiscard]] DescriptionLevel getActiveDescLevel() const { return descLevel_; }

    //! Writes a formatted description of options in this context.
    OptionOutput& description(OptionOutput& out) const;

    //! Returns the default command-line of this context.
    [[nodiscard]] std::string defaults(std::size_t prefixSize = 0) const;

    //! Writes a formatted description of options in this context to os.
    friend std::ostream& operator<<(std::ostream& os, const OptionContext& ctx);

    //! Assigns any default values to all options not in exclude.
    /*!
     * \throw ValueError if some default value is actually invalid for its option.
     */
    void assignDefaults(const ParsedOptions& exclude) const;

private:
    [[nodiscard]] size_t findGroupKey(std::string_view name) const;
    [[nodiscard]] size_t findOption(std::string_view name, FindType t) const;
    void                 insertOption(size_t groupId, const SharedOptPtr& o);

    Name2Key         index_;
    OptionList       options_;
    GroupList        groups_;
    std::string      caption_;
    DescriptionLevel descLevel_;
};

//! Set of options holding a parsed value.
class ParsedOptions {
public:
    ParsedOptions();
    ~ParsedOptions();
    [[nodiscard]] bool        empty() const { return parsed_.empty(); }
    [[nodiscard]] std::size_t size() const { return parsed_.size(); }
    [[nodiscard]] bool        contains(std::string_view name) const { return parsed_.contains(name); }

    void add(std::string_view name) { parsed_.emplace(name); }
    void merge(ParsedOptions&& other);

private:
    std::set<std::string, std::less<>> parsed_;
};

class ParseContext {
public:
    using FindType = OptionContext::FindType;
    explicit ParseContext(std::string_view name);
    virtual ~ParseContext();
    [[nodiscard]] auto name() const -> std::string_view { return name_; }

    auto getOption(std::string_view name, FindType ft) -> SharedOptPtr;
    void setValue(const SharedOptPtr& opt, std::string_view value);
    void finish(const std::exception_ptr& error);

protected:
    enum class OptState {
        state_open,
        state_seen,
        state_skip,
    };
    [[nodiscard]] virtual auto state(const Option& opt) const -> OptState = 0;

    virtual auto doGetOption(std::string_view name, FindType ft) -> SharedOptPtr = 0;
    virtual bool doSetValue(const SharedOptPtr& opt, std::string_view value)     = 0;
    virtual void doFinish(const std::exception_ptr& error)                       = 0;

private:
    std::string_view name_;
};

//! Default parsing context to be used with parsing functions.
class DefaultParseContext : public ParseContext {
public:
    /*!
     * Creates a context over the given options.
     * \param o  Set of known options.
     */
    explicit DefaultParseContext(const OptionContext& o);

    auto parsed() -> const ParsedOptions&;
    auto clearParsed() -> DefaultParseContext&;

private:
    [[nodiscard]] auto state(const Option& opt) const -> OptState override;

    auto doGetOption(std::string_view name, FindType ft) -> SharedOptPtr override;
    bool doSetValue(const SharedOptPtr& opt, std::string_view value) override;
    void doFinish(const std::exception_ptr&) override;

    const OptionContext* ctx_;
    ParsedOptions        parsed_;
    ParsedOptions        seen_;
};

//! Base class for options parsers.
class OptionParser {
public:
    using FindType = OptionContext::FindType;
    explicit OptionParser(ParseContext& ctx);
    virtual ~OptionParser();
    ParseContext& parse();

protected:
    [[nodiscard]] ParseContext& ctx() const { return *ctx_; }
    [[nodiscard]] SharedOptPtr  getOption(std::string_view name, FindType ft) const;
    void                        applyValue(const SharedOptPtr& key, std::string_view value);

private:
    virtual void  doParse() = 0;
    ParseContext* ctx_;
};

//! Default formatting for options.
struct DefaultFormat {
    static std::size_t format(std::string&, const OptionContext&) { return 0; }
    //! Writes g.caption() to buffer.
    static std::size_t format(std::string& buffer, const OptionGroup& g);
    //! Writes long name, short name, and argument name to buffer.
    static std::size_t format(std::string& buffer, const Option& o, std::size_t maxW);
    //! Writes description to buffer.
    /*!
     * Occurrences of %D, %I, and %A in desc are replaced with
     * the value's default value, implicit value, and name, respectively.
     */
    static std::size_t format(std::string& buffer, std::string_view desc, const Value&, std::string_view valSep = ": ");
};

//! Base class for printing options.
class OptionOutput {
public:
    OptionOutput()                                                = default;
    virtual ~OptionOutput()                                       = default;
    virtual bool printContext(const OptionContext& ctx)           = 0;
    virtual bool printGroup(const OptionGroup& group)             = 0;
    virtual bool printOption(const Option& opt, std::size_t maxW) = 0;
};

//! Implementation class for printing options.
template <typename Formatter = DefaultFormat>
class OptionOutputImpl : public OptionOutput {
public:
    using Sink = std::function<void(std::string_view)>;
    //! Writes formatted option descriptions to the given FILE.
    explicit OptionOutputImpl(FILE* f, const Formatter& form = Formatter())
        : OptionOutputImpl([f](std::string_view view) { fwrite(view.data(), 1, view.size(), f); }, form) {}
    //! Writes formatted option descriptions to given std::string.
    explicit OptionOutputImpl(std::string& str, const Formatter& form = Formatter())
        : OptionOutputImpl([&str](std::string_view view) { str.append(std::data(view), std::size(view)); }, form) {}
    //! Writes formatted option descriptions to given std::ostream.
    explicit OptionOutputImpl(std::ostream& os, const Formatter& form = Formatter())
        : OptionOutputImpl([&os](std::string_view view) { os.write(std::data(view), std::ssize(view)); }, form) {}
    //! Writes formatted option descriptions to given sink.
    explicit OptionOutputImpl(Sink sink, const Formatter& form = Formatter())
        : sink_(std::move(sink))
        , formatter_(form) {}

    bool printContext(const OptionContext& ctx) override {
        writeBuffer(formatter_.format(buffer_, ctx));
        return true;
    }
    bool printGroup(const OptionGroup& group) override {
        writeBuffer(formatter_.format(buffer_, group));
        return true;
    }
    bool printOption(const Option& opt, std::size_t maxW) override {
        writeBuffer(formatter_.format(buffer_, opt, maxW));
        writeBuffer(formatter_.format(buffer_, opt.description(), *opt.value()));
        return true;
    }

private:
    void writeBuffer(std::size_t n) {
        if (sink_ && n) {
            sink_(std::string_view{buffer_.data(), n});
        }
        buffer_.clear();
    }
    std::string buffer_;
    Sink        sink_;
    Formatter   formatter_;
};
using OptionPrinter = OptionOutputImpl<>;
///////////////////////////////////////////////////////////////////////////////
// parse functions
///////////////////////////////////////////////////////////////////////////////
/*!
 * A function type for processing tokens that have no option name.
 * Concrete functions shall either return true and store the name of the option that
 * should receive the token as value in its second argument or return false to signal an error.
 */
using PosOption = std::function<bool(std::string_view, std::string&)>;

enum CommandLineFlags {
    command_line_allow_flag_value = 1u, //!< Allow explicit values even for flags (e.g., --flag=1)
};

//! Parses the command arguments given in `args`.
/*!
 * \param ctx   Parse context to use for looking up and assigning found options.
 * \param args  The arguments to parse.
 * \param flags Optional config flags (see CommandLineFlags).
 *
 * \return The given parse context.
 *
 * \throw SyntaxError if argument syntax is incorrect.
 * \throw UnknownOption if an argument is found that does not match any option.
 */
ParseContext& parseCommandArray(ParseContext& ctx, std::span<const char* const> args, PosOption pos = nullptr,
                                unsigned flags = 0);

//! Parses the command line given in `args`.
/*!
 * \copydetails parseCommandArray
 */
ParseContext& parseCommandString(ParseContext& ctx, std::string_view args, PosOption pos = nullptr,
                                 unsigned flags = command_line_allow_flag_value);

//! Parses a config file having the format key = value.
/*!
 * \note Keys are option's long names.
 * \note Lines starting with # are treated as comments and are ignored.
 *
 * \param ctx Parse context to use for looking up and assigning found options.
 * \param is  The stream representing the config file.
 *
 * \return The given parse context.
 *
 * \throw SyntaxError if config file syntax is incorrect.
 * \throw UnknownOption if a key is found that does not match any option.
 */
ParseContext& parseCfgFile(ParseContext& ctx, std::istream& is);

} // namespace Potassco::ProgramOptions
