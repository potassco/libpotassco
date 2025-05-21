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
#include <potassco/program_opts/value.h>

#include <cstdio>
#include <functional>
#include <iosfwd>
#include <map>
#include <ostream>
#include <set>
#include <span>
#include <vector>

namespace Potassco::ProgramOptions {

//! Represents a program option.
/*!
 * An Option consists of a description (long name, short name, description),
 * a (typed) value, and an optional default value.
 */
class Option {
public:
    /*!
     * \pre not name.empty()
     * \param name        name (and unique key) of the option
     * \param description description of the option, used for printing help
     * \param value       value description of the option
     * \param alias       optional single character alias (0 for none)
     */
    Option(Str name, Str description, ValueDesc&& value, char alias = 0);
    ~Option();
    Option(const Option&)            = delete;
    Option& operator=(const Option&) = delete;

    [[nodiscard]] auto name() const -> std::string_view { return str_[str_name]; }
    [[nodiscard]] auto id() const -> uint32_t { return id_; }
    [[nodiscard]] char alias() const { return alias_; }
    [[nodiscard]] auto description() const -> std::string_view { return str_[str_desc]; }
    [[nodiscard]] auto argName() const -> std::string_view { return str_[str_arg]; }
    [[nodiscard]] auto defaultValue() const -> std::string_view { return str_[str_def]; }
    [[nodiscard]] auto implicitValue() const -> std::string_view { return str_[str_imp]; }
    [[nodiscard]] auto maxColumn() const -> std::size_t;
    [[nodiscard]] auto descLevel() const -> DescriptionLevel { return static_cast<DescriptionLevel>(level_); }
    [[nodiscard]] bool negatable() const { return negatable_ != 0; }
    [[nodiscard]] bool composing() const { return composing_ != 0; }
    [[nodiscard]] bool implicit() const { return implicit_ != 0; }
    [[nodiscard]] bool flag() const { return flag_ != 0; }
    [[nodiscard]] bool defaulted() const { return defaulted_ != 0; }
    //! Writes the option's description to out.
    /*!
     * Occurrences of %D, %I, and %A in the description are replaced
     * with the option's default value, implicit value, and the argument name, respectively.
     */
    std::string& description(std::string& out) const;

    //! Assigns the given value to the option.
    [[nodiscard]] bool assign(std::string_view value);
    //! Assigns the option's default value if it has one.
    [[nodiscard]] bool assignDefault();

private:
    friend int  intrusiveRelease(Option* o) { return --o->refCount_; }
    friend void intrusiveAddRef(Option* o) { ++o->refCount_; }
    friend int  intrusiveCount(const Option* o) { return o->refCount_; }
    enum StrType : uint32_t { str_name = 0u, str_desc = 1u, str_arg = 2u, str_imp = 3u, str_def = 4u };
    bool assign(std::string_view value, bool def);
    void init(StrType t, const Str& str);

    const char*    str_[5]{};          // strings (one for each StrType).
    ValueActionPtr action_;            // assign action
    uint32_t       id_{0};             // optional numeric id
    int32_t        refCount_{1};       // intrusive ref-count
    char           alias_{0};          // optional single char alias
    uint8_t        level_     : 3 {0}; // help level
    uint8_t        implicit_  : 1 {0}; // implicit value?
    uint8_t        flag_      : 1 {0}; // implicit and type bool?
    uint8_t        composing_ : 1 {0}; // multiple values allowed?
    uint8_t        negatable_ : 1 {0}; // negatable form allowed?
    uint8_t        defaulted_ : 1 {0}; // default value assigned?
    uint8_t        own_{0};            // bitset of owned strings
};

class OptionParser;
class ParsedOptions;
class OptionOutput;

//! A list of options logically grouped under a caption.
/*!
 * The class provides a logical grouping of options mainly useful for printing help.
 */
class OptionGroup {
public:
    using SharedOption = IntrusiveSharedPtr<Option>;

    //! Creates a new group of options under the given caption.
    explicit OptionGroup(std::string_view caption = "", DescriptionLevel descLevel = desc_level_default);

    //! Returns the caption of this group.
    [[nodiscard]] std::string_view caption() const { return caption_; }

    //! Returns whether the group does not contain any options.
    [[nodiscard]] bool empty() const { return options_.empty(); }
    //! Returns the number of options in this group.
    [[nodiscard]] std::size_t size() const { return options_.size(); }
    //! Returns the options in this group.
    [[nodiscard]] auto options() const -> std::span<const SharedOption> { return std::span{options_}; }
    //! Returns the description level of this group.
    [[nodiscard]] DescriptionLevel descLevel() const { return level_; }
    //! Returns an option with the given name or nullptr if the group has no such option.
    [[nodiscard]] Option* find(std::string_view name) const;
    //! Returns an option with the given alias or nullptr if the group has no such option.
    [[nodiscard]] Option* find(char alias) const;
    //! Returns the ith option in this group.
    [[nodiscard]] auto operator[](std::size_t i) const -> const SharedOption& {
        assert(i < size());
        return options_[i];
    }

    //! Helper class for adding options to a group.
    class Init {
    public:
        //! Applies the given spec.
        /*!
         * Parses and applies a spec like `[!][+][*][-<alias>][@<level>]` such that:
         *  - `!` is mapped to `value.negatable()`,
         *  - `+` is mapped to `value.composing()`,
         *  - `*` is mapped to `value.flag()`,
         *  - `-<alias>` is mapped to `alias`, and
         *  - `@<level>` is mapped `value.level(enum_cast<DescriptionLevel>(level))`
         *
         *  \return true if `spec` is valid, i.e., elements are either not present or valid.
         */
        static bool applySpec(std::string_view spec, ValueDesc& value, char& alias);

        explicit Init(OptionGroup& owner);

        //! Factory function for adding an option to the group given on construction.
        /*!
         * \param name  Name (and unique key) of the new option
         * \param spec  Additional specification (see applySpec(std::string_view, ValueDesc&, char&))
         * \param value Value description of the option
         * \param desc  Description of the option
         *
         * \throw Error if `name` is empty or `spec` is not valid.
         */
        Init& operator()(Str name, std::string_view spec, ValueDesc value, Str desc);

        /*!
         * \overload Init::operator()(Str, std::string_view, ValueDesc, Str)
         */
        Init& operator()(Str name, ValueDesc value, Str desc);

    private:
        OptionGroup* owner_;
    };

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
    Init addOptions();

    //! Adds the given option to this group.
    void addOption(std::unique_ptr<Option> option);
    void addOption(SharedOption option);

    void setDescriptionLevel(DescriptionLevel level) { level_ = level; }

    //! Creates a formatted description of all options with level() <= level in this group.
    void format(OptionOutput& out, size_t maxW, DescriptionLevel level = desc_level_default) const;

    [[nodiscard]] std::size_t maxColumn(DescriptionLevel level) const;

private:
    friend class OptionContext;
    using OptionList = std::vector<SharedOption>;
    std::string      caption_;
    OptionList       options_;
    DescriptionLevel level_;
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
public:
    using OptionList = OptionGroup::OptionList;

    explicit OptionContext(std::string_view caption = "", DescriptionLevel desc_default = desc_level_default);

    [[nodiscard]] std::string_view caption() const;

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
    OptionContext& add(OptionGroup&& group);

    //! Adds an alias name for the option with the given index.
    /*!
     * \throw DuplicateOption if an option with the name aliasName already exists.
     * \see index(std::string_view)
     */
    OptionContext& addAlias(std::size_t idx, std::string_view aliasName);

    //! Adds all groups (and their options) from other to this context.
    /*!
     * \throw DuplicateOption if an option in other
     *        has the same short or long name as one of the
     *        options in this context.
     *
     * \see OptionContext& add(const OptionGroup&);
     */
    OptionContext& add(const OptionContext& other);

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
    [[nodiscard]] auto option(std::string_view name, FindType t = find_name) const -> Option&;
    [[nodiscard]] auto operator[](std::string_view name) const -> const Option&;

    //! Returns the index of the option with the given name or prefix.
    /*!
     * \throw UnknownOption if no option matches `name`.
     * \throw AmbiguousOption if more than one option matches `name`.
     */
    [[nodiscard]] auto index(std::string_view name, FindType t = find_name) const -> std::size_t;

    //! Returns the option group with the given caption or throws `ContextError` if no such group exists.
    [[nodiscard]] auto group(std::string_view caption) const -> const OptionGroup&;
    //! Sets the description level to be used when generating a description.
    /*!
     * Once set, functions generating descriptions will only consider groups
     * and options with description level <= std::min(level, desc_level_all).
     */
    void               setActiveDescLevel(DescriptionLevel level);
    [[nodiscard]] auto getActiveDescLevel() const -> DescriptionLevel { return descLevel_; }

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
    using KeyType   = std::size_t;
    using Name2Key  = std::map<std::string, KeyType, std::less<>>;
    using GroupList = std::vector<OptionGroup>;

    [[nodiscard]] size_t findGroupKey(std::string_view name) const;
    [[nodiscard]] size_t findOption(std::string_view name, FindType t) const;
    void                 addToIndex(const OptionGroup::SharedOption& opt);
    OptionGroup&         addGroup(std::string_view name, DescriptionLevel level);

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

    auto getOption(std::string_view name, FindType ft) -> Option*;
    void setValue(Option& opt, std::string_view value);
    void finish(const std::exception_ptr& error);

protected:
    enum class OptState {
        state_open,
        state_seen,
        state_skip,
    };
    [[nodiscard]] virtual auto state(const Option& opt) const -> OptState = 0;

    virtual auto doGetOption(std::string_view name, FindType ft) -> Option* = 0;
    virtual bool doSetValue(Option& opt, std::string_view value)            = 0;
    virtual void doFinish(const std::exception_ptr& error)                  = 0;

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

    auto doGetOption(std::string_view name, FindType ft) -> Option* override;
    bool doSetValue(Option& opt, std::string_view value) override;
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
    [[nodiscard]] Option&       getOption(std::string_view name, FindType ft) const;
    void                        applyValue(Option& opt, std::string_view value);

private:
    virtual void  doParse() = 0;
    ParseContext* ctx_;
};

//! Default formatting for options.
struct DefaultFormat {
    static std::size_t format(std::string&, const OptionContext&) { return 0; }
    //! Writes g.caption() to buffer.
    static std::size_t format(std::string& buffer, const OptionGroup& g);
    //! Writes short, long, and argument name followed by option description to buffer.
    static std::size_t format(std::string& buffer, const Option& o, std::size_t colWidth);
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
