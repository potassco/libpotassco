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
#include <potassco/program_opts/program_options.h>

#include <potassco/program_opts/errors.h>

#include <potassco/bits.h>

#include <algorithm> // std::sort
#include <cassert>
#include <cctype>
#include <climits>
#include <cstring>
#include <istream> // for CfgFileParser
#include <ostream> // for op<<
#include <span>
#include <utility>

namespace Potassco::ProgramOptions {
using namespace std::literals;
///////////////////////////////////////////////////////////////////////////////
// DefaultFormat
///////////////////////////////////////////////////////////////////////////////
static std::string quote(std::string_view x) { return std::string(1, '\'').append(x).append(1, '\''); }
std::size_t        DefaultFormat::format(std::string& buffer, const Option& o, std::size_t maxW) {
    auto bufSize = std::max(maxW, o.maxColumn()) + 3;
    auto arg     = o.argName();
    auto np      = ""sv;
    auto ap      = ""sv;
    if (o.value()->isNegatable()) {
        if (arg.empty()) {
            np = "[no-]"sv;
        }
        else {
            ap       = "|no"sv;
            bufSize += ap.size();
        }
    }
    const auto startSize = buffer.size();
    buffer.reserve(startSize + bufSize);
    buffer.append("  "sv);
    if (o.alias()) {
        buffer.append(1, '-').append(1, o.alias()).append(",");
    }
    buffer.append("--"sv).append(np).append(o.name());
    if (o.value()->isImplicit() && not arg.empty()) {
        buffer.append("[="sv).append(arg).append(ap).append("]"sv);
    }
    if (not o.value()->isImplicit()) {
        buffer.append(1, not o.alias() ? '=' : ' ').append(arg).append(ap);
    }
    if (auto sz = buffer.size() - startSize; sz < maxW) {
        buffer.append(maxW - sz, ' ');
    }
    return buffer.size() - startSize;
}
std::size_t DefaultFormat::format(std::string& buffer, std::string_view desc, const Value& val,
                                  std::string_view valSep) {
    const auto startSize = buffer.size();
    buffer.reserve(startSize + desc.length() + valSep.length());
    buffer.append(valSep);
    for (;;) {
        auto next = desc.substr(0, desc.find('%'));
        buffer.append(next);
        desc.remove_prefix(std::min(next.length() + 1, desc.length()));
        if (desc.empty()) {
            break;
        }
        const char* replace = nullptr;
        switch (desc.front()) {
            case 'A': replace = val.arg(); break;
            case 'D': replace = val.defaultsTo(); break;
            case 'I': replace = val.implicit(); break;
            default : buffer.push_back(desc.front()); break;
        }
        desc.remove_prefix(1u);
        buffer.append(replace ? replace : "");
    }
    buffer.push_back('\n');
    return buffer.size() - startSize;
}
std::size_t DefaultFormat::format(std::string& buffer, const OptionGroup& grp) {
    const auto startSize = buffer.size();
    if (auto length = grp.caption().length(); length) {
        buffer.reserve(startSize + length + 4);
        buffer.append(1, '\n').append(grp.caption()).append(1, ':').append(2, '\n');
    }
    return buffer.size() - startSize;
}
const char* Str::clone(Str str) {
    auto tmp = std::make_unique<char[]>(std::strlen(str.c_str()) + 1);
    std::strcpy(tmp.get(), str.c_str());
    return tmp.release();
}
///////////////////////////////////////////////////////////////////////////////
// class Value
///////////////////////////////////////////////////////////////////////////////
Value::Value(uint8_t id) : id_(id) {}
Value::~Value() {
    static_assert(sizeof(Value) == sizeof(void*) * 3, "unexpected size");
    for (auto x : {desc_name, desc_default, desc_implicit}) { clear(x); }
    if (descVal_ == desc_pack) {
        delete[] desc_.pack;
    }
}
void Value::clear(DescType t) {
    if (test_bit(own_, t)) {
        delete[] desc(t);
        own_ = clear_bit(own_, t);
    }
}
const char* Value::arg() const {
    if (const char* x = desc(desc_name)) {
        return x;
    }
    return isFlag() ? "" : "<arg>";
}

Value* Value::desc(DescType t, Str str) {
    const char** target = nullptr;
    auto         newTag = descVal_;
    if (descVal_ == t || (descVal_ != desc_pack && desc_.value == nullptr)) {
        target = &desc_.value;
        newTag = t;
    }
    else {
        if (descVal_ != desc_pack) {
            auto* pack     = new const char*[3]{};
            pack[descVal_] = desc_.value;
            desc_.pack     = pack;
            newTag = descVal_ = desc_pack;
        }
        target = &desc_.pack[t];
    }
    const char* source = str.isLit() ? str.c_str() : Str::clone(str);
    clear(t);
    *target = source;
    if (not str.isLit()) {
        own_ = set_bit(own_, t);
    }
    descVal_  = newTag;
    implicit_ = implicit_ || t == desc_implicit;
    return this;
}

const char* Value::desc(DescType t) const {
    if (descVal_ == desc_pack) {
        return desc_.pack[t];
    }
    return t == descVal_ ? desc_.value : nullptr;
}

const char* Value::implicit() const {
    if (implicit_ == 0) {
        return nullptr;
    }
    const char* x = desc(desc_implicit);
    return x ? x : "1";
}

bool Value::parse(std::string_view name, std::string_view value, bool def) {
    if (value.empty() && isImplicit()) {
        value = implicit();
    }
    if (doParse(name, value)) {
        defaulted_ = def;
        return true;
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////
// class Option
///////////////////////////////////////////////////////////////////////////////
Option::Option(Str name, Str description, std::unique_ptr<Value> v)
    : name_(name.c_str())
    , description_(description.c_str())
    , value_(std::move(v)) {
    assert(value_);
    assert(not name.empty());
    if (not name.isLit()) {
        name_ = Str::clone(name);
        value_->rtName(true);
    }
    if (not description.isLit()) {
        description_ = Str::clone(description);
        value_->rtDesc(true);
    }
}
Option::Option(Str name, Str description, Value* value) : Option(name, description, std::unique_ptr<Value>(value)) {}
Option::Option(Str name, char alias, Str description, Value* value)
    : Option(name, description, std::unique_ptr<Value>(value)) {
    value_->alias(alias);
}
Option::~Option() {
    if (value_) {
        if (value_->rtDesc()) {
            delete[] description_;
        }
        if (value_->rtName()) {
            delete[] name_;
        }
    }
}

std::size_t Option::maxColumn() const {
    auto col = 4 + name().size(); //  --name
    if (alias()) {
        col += 3; // ,-o
    }
    if (auto argN = argName().size()) {
        col += (argN + 1); // =arg
        if (value()->isImplicit()) {
            col += 2; // []
        }
        if (value()->isNegatable()) {
            col += 3; // |no
        }
    }
    else if (value()->isNegatable()) {
        col += 5; // [no-]
    }
    return col;
}

bool Option::assignDefault() const {
    if (value()->defaultsTo() != nullptr && not value()->isDefaulted()) {
        return value()->parse(name(), value()->defaultsTo(), true);
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////
// class OptionGroup
///////////////////////////////////////////////////////////////////////////////
OptionGroup::OptionGroup(std::string_view caption, DescriptionLevel hl) : caption_(caption), level_(hl) {}

OptionInitHelper OptionGroup::addOptions() { return OptionInitHelper(*this); }

void OptionGroup::addOption(const SharedOptPtr& option) { options_.push_back(option); }

std::size_t OptionGroup::maxColumn(DescriptionLevel level) const {
    std::size_t maxW = 0;
    for (const auto& opt : options_) {
        if (opt->descLevel() <= level) {
            maxW = std::max(maxW, opt->maxColumn());
        }
    }
    return maxW;
}

void OptionGroup::format(OptionOutput& out, size_t maxW, DescriptionLevel level) const {
    for (const auto& opt : options_) {
        if (opt->descLevel() <= level) {
            out.printOption(*opt, maxW);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////
// class OptionInitHelper
///////////////////////////////////////////////////////////////////////////////
bool OptionInitHelper::applySpec(std::string_view spec, Value& value) {
    for (uint32_t seen = 0; not spec.empty();) {
        auto p = "+!*-@"sv.find(spec.front());
        if (p == std::string_view::npos || test_bit(seen, p)) {
            break;
        }
        store_set_bit(seen, p);
        if (char n = spec.front(); n == '+') {
            value.composing();
            spec.remove_prefix(1);
        }
        else if (n == '!') {
            value.negatable();
            spec.remove_prefix(1);
        }
        else if (n == '*') {
            value.flag();
            spec.remove_prefix(1);
        }
        else if (n == '-' && spec.size() > 1) {
            value.alias(spec[1]);
            spec.remove_prefix(2);
        }
        else if (n == '@' && spec.size() > 1) {
            if (auto l = static_cast<unsigned>(spec[1]) - static_cast<unsigned>('0'); l <= desc_level_hidden) {
                value.level(static_cast<DescriptionLevel>(l));
                spec.remove_prefix(2);
            }
        }
    }
    return spec.empty();
}
OptionInitHelper::OptionInitHelper(OptionGroup& owner) : owner_(&owner) {}
OptionInitHelper& OptionInitHelper::operator()(Str name, std::string_view spec, Value* val, Str desc) {
    std::unique_ptr<Value> value(val);
    if (name.empty()) {
        throw Error("Invalid empty option name");
    }
    if (std::strchr(name.c_str(), ',') != nullptr) {
        throw Error("Invalid comma in name "s.append(quote(name.c_str())));
    }
    if (not applySpec(spec, *value)) {
        throw Error("Invalid option spec "s.append(quote(spec)).append(" for option ").append(quote(name.c_str())));
    }
    owner_->addOption(SharedOptPtr(new Option(name, desc, std::move(value))));
    return *this;
}
OptionInitHelper& OptionInitHelper::operator()(Str nameSpec, Value* val, Str desc) {
    std::string_view spec{};
    if (const auto* sep = std::strchr(nameSpec.c_str(), ','); sep) {
        auto sz = static_cast<std::size_t>(sep - nameSpec.c_str());
        spec    = {nameSpec.c_str(), sz};
        nameSpec.removePrefix(sz + 1);
    }
    return this->operator()(nameSpec, spec, val, desc);
}
///////////////////////////////////////////////////////////////////////////////
// class OptionContext
///////////////////////////////////////////////////////////////////////////////
OptionContext::OptionContext(std::string_view caption, DescriptionLevel def) : caption_(caption), descLevel_(def) {}

auto OptionContext::caption() const -> const std::string& { return caption_; }
void OptionContext::setActiveDescLevel(DescriptionLevel level) { descLevel_ = std::min(level, desc_level_all); }
auto OptionContext::findGroupKey(std::string_view name) const -> std::size_t {
    auto it = std::ranges::find_if(groups_, [&](const OptionGroup& grp) { return grp.caption() == name; });
    return it != groups_.end() ? static_cast<std::size_t>(it - groups_.begin()) : static_cast<std::size_t>(-1);
}

OptionContext& OptionContext::add(const OptionGroup& group) {
    auto k = findGroupKey(group.caption());
    if (k >= groups_.size()) {
        // add as a new group
        k = groups_.size();
        groups_.emplace_back(group.caption(), group.descLevel());
    }
    for (const auto& opt : group) { insertOption(k, opt); }
    groups_[k].setDescriptionLevel(std::min(group.descLevel(), groups_[k].descLevel()));
    return *this;
}

OptionContext& OptionContext::addAlias(std::size_t pos, std::string_view aliasName) {
    if (pos < options_.size() && not aliasName.empty()) {
        auto [it, ins] = index_.try_emplace(std::string{aliasName}, pos);
        if (not ins) {
            throw DuplicateOption(caption(), it->first);
        }
    }
    return *this;
}
const OptionGroup& OptionContext::group(std::string_view name) const {
    if (auto x = findGroupKey(name); x < groups_.size()) {
        return groups_[x];
    }
    throw ContextError(caption(), ContextError::unknown_group, name);
}
OptionContext& OptionContext::add(const OptionContext& other) {
    if (this == &other) {
        return *this;
    }
    for (const auto& grp : other.groups_) { add(grp); }
    return *this;
}

void OptionContext::insertOption(std::size_t groupId, const SharedOptPtr& opt) {
    auto    l = opt->name();
    KeyType k(options_.size());
    if (opt->alias()) {
        if (char sName[2] = {'-', opt->alias()}; not index_.try_emplace({sName, 2}, k).second) {
            throw DuplicateOption(caption(), l);
        }
    }
    if (not index_.try_emplace(std::string{l}, k).second) {
        throw DuplicateOption(caption(), l);
    }
    options_.push_back(opt);
    groups_[groupId].options_.push_back(opt);
}
auto OptionContext::option(std::string_view name, FindType t) const -> SharedOptPtr {
    return options_[findOption(name, t)];
}
auto OptionContext::operator[](std::string_view name) const -> const Option& { return *option(name); }
auto OptionContext::optionIndex(std::string_view name) const -> std::size_t { return findOption(name, find_name); }
auto OptionContext::findOption(std::string_view name, FindType t) const -> std::size_t {
    std::string alias;
    if (t == find_alias && not name.starts_with('-')) {
        alias.append(1, '-').append(name);
        name = alias;
    }
    if (auto it = index_.lower_bound(name); it != index_.end()) {
        if ((it->first == name) && ((t & (find_alias | find_name)) != 0)) {
            return it->second;
        }
        if ((t & find_prefix) != 0 && it->first.starts_with(name)) {
            if (auto n = std::next(it); n == index_.end() || not n->first.starts_with(name)) {
                return it->second;
            }
            std::string str;
            do {
                str += "  ";
                str += it->first;
                str += "\n";
            } while (++it != index_.end() && it->first.starts_with(name));
            throw AmbiguousOption(caption(), name, str);
        }
    }
    throw UnknownOption(caption(), name);
}

OptionOutput& OptionContext::description(OptionOutput& out) const {
    DescriptionLevel dl = descLevel_;
    if (out.printContext(*this) && not groups_.empty()) {
        std::size_t maxW = 23;
        for (const auto& grp : groups_) { maxW = std::max(maxW, grp.maxColumn(dl)); }
        // print all visible groups
        for (const auto& grp : std::span{groups_}.subspan(1)) {
            if (grp.descLevel() <= dl && out.printGroup(grp)) {
                grp.format(out, maxW, dl);
            }
        }
        if (groups_[0].descLevel() <= dl && out.printGroup(groups_[0])) {
            groups_[0].format(out, maxW, dl);
        }
    }
    return out;
}
static void appendDefaults(std::string& out, const OptionGroup& group, DescriptionLevel level, std::string& tmp,
                           std::size_t& written, std::size_t prefixLen) {
    if (group.descLevel() > level) {
        return;
    }
    auto space = static_cast<unsigned>(not out.empty() && not out.ends_with(' '));
    for (const auto& opt : group) {
        if (opt->value()->defaultsTo() && opt->descLevel() <= level) {
            tmp.append(space, ' ').append("--"sv).append(opt->name()).append(1, '=').append(opt->value()->defaultsTo());
            if (tmp.size() + written > 78) {
                out.append(1, '\n').append(written = prefixLen, ' ');
            }
            out.append(tmp);
            written += tmp.size();
            tmp.clear();
            space = 1;
        }
    }
}
std::string OptionContext::defaults(std::size_t n) const {
    std::string defs;
    if (not groups_.empty()) {
        std::string tmp;
        auto        written = n;
        // print all subgroups followed by the main group
        for (const auto& grp : std::span{groups_}.subspan(1)) {
            appendDefaults(defs, grp, descLevel_, tmp, written, n);
        }
        appendDefaults(defs, groups_[0], descLevel_, tmp, written, n);
    }
    return defs;
}
std::ostream& operator<<(std::ostream& os, const OptionContext& grp) {
    OptionPrinter out(os);
    grp.description(out);
    return os;
}

void OptionContext::assignDefaults(const ParsedOptions& opts) const {
    for (const auto& optPtr : *this) {
        const Option& o = *optPtr;
        if (not opts.contains(o.name()) && not o.assignDefault()) {
            throw ValueError(caption(), ValueError::invalid_default, o.name(), o.value()->defaultsTo());
        }
    }
}
///////////////////////////////////////////////////////////////////////////////
// class ParsedOptions
///////////////////////////////////////////////////////////////////////////////
ParsedOptions::ParsedOptions() = default;
ParsedOptions::~ParsedOptions() { parsed_.clear(); }
void ParsedOptions::merge(ParsedOptions&& other) { parsed_.merge(std::move(other.parsed_)); }
///////////////////////////////////////////////////////////////////////////////
// class OptionParser
///////////////////////////////////////////////////////////////////////////////
OptionParser::OptionParser(ParseContext& o) : ctx_(&o) {}

OptionParser::~OptionParser() = default;
auto OptionParser::getOption(std::string_view name, FindType ft) const -> SharedOptPtr {
    if (auto opt = ctx_->getOption(name, ft); opt) {
        return opt;
    }
    throw UnknownOption(ctx_->name(), name);
}
void OptionParser::applyValue(const SharedOptPtr& key, std::string_view value) { ctx_->setValue(key, value); }

ParseContext& OptionParser::parse() {
    try {
        doParse();
        ctx_->finish(nullptr);
        return *ctx_;
    }
    catch (...) {
        ctx_->finish(std::current_exception());
        throw;
    }
}
ParseContext::ParseContext(std::string_view name) : name_(name) {}
ParseContext::~ParseContext() = default;
auto ParseContext::getOption(std::string_view name, FindType ft) -> SharedOptPtr { return doGetOption(name, ft); }
void ParseContext::setValue(const SharedOptPtr& opt, std::string_view value) {
    if (not opt->value()->isComposing()) {
        auto st = state(*opt);
        if (st == OptState::state_skip) {
            return;
        }
        if (st == OptState::state_seen) {
            throw ValueError(name_, ValueError::multiple_occurrences, opt->name(), value);
        }
    }
    if (not doSetValue(opt, value)) {
        throw ValueError(name_, ValueError::invalid_value, opt->name(), value);
    }
}
void ParseContext::finish(const std::exception_ptr& error) { doFinish(error); }

DefaultParseContext::DefaultParseContext(const OptionContext& o) : ParseContext(o.caption()), ctx_(&o) {}

auto DefaultParseContext::state(const Option& opt) const -> OptState {
    if (parsed_.contains(opt.name())) {
        return OptState::state_skip;
    }
    if (seen_.contains(opt.name())) {
        return OptState::state_seen;
    }
    return OptState::state_open;
}
auto DefaultParseContext::doGetOption(std::string_view name, FindType ft) -> SharedOptPtr {
    return ctx_->option(name, ft);
}
bool DefaultParseContext::doSetValue(const SharedOptPtr& opt, std::string_view value) {
    if (not opt->value()->parse(opt->name(), value)) {
        return false;
    }
    seen_.add(opt->name());
    return true;
}
void DefaultParseContext::doFinish(const std::exception_ptr&) {
    parsed_.merge(std::move(seen_));
    seen_ = {};
}
auto DefaultParseContext::clearParsed() -> DefaultParseContext& {
    parsed_ = {};
    return *this;
}
auto DefaultParseContext::parsed() -> const ParsedOptions& { return parsed_; }

namespace {
///////////////////////////////////////////////////////////////////////////////
// class CommandLineParser
///////////////////////////////////////////////////////////////////////////////
class CommandLineParser : public OptionParser {
public:
    CommandLineParser(ParseContext& ctx, PosOption posOpt, unsigned f)
        : OptionParser(ctx)
        , posOpt_(std::move(posOpt))
        , flags_(f) {}

private:
    virtual std::string_view next() = 0;
    void                     doParse() override {
        for (std::string_view curr; not(curr = next()).empty() && curr != "--"sv;) {
            if (curr.starts_with("--")) {
                handleLongOpt(curr.substr(2));
            }
            else if (curr.starts_with('-')) {
                handleShortOpt(curr.substr(1));
            }
            else {
                handlePositionalOpt(curr);
            }
        }
    }
    template <typename ErrorCb>
    [[nodiscard]] auto getOpt(std::string_view optName, ErrorCb&& cb) const -> SharedOptPtr {
        try {
            return getOption(optName, OptionContext::find_name_or_prefix);
        }
        catch (const ContextError& error) {
            return std::forward<ErrorCb>(cb)(error.type());
        }
        catch (...) {
            return std::forward<ErrorCb>(cb)(static_cast<ContextError::Type>(-1));
        }
    }
    void handleShortOpt(std::string_view optName) {
        // either -o value or -o[value|opts]
        char             tmp[2] = {'-', 0};
        std::string_view opt(tmp, 2);
        while (not optName.empty()) {
            tmp[1]   = optName.front();
            auto val = optName.substr(1);
            auto o   = getOption(opt, OptionContext::find_alias);
            if (o->value()->isImplicit()) {
                // -ovalue or -oopts
                if (not o->value()->isFlag()) {
                    // consume (possibly empty) value
                    applyValue(o, val);
                    return;
                }
                // -o + more options
                applyValue(o, {});
                optName.remove_prefix(1);
            }
            else if (val = val.empty() ? next() : val; not val.empty()) {
                //  -ovalue or -o value
                applyValue(o, val);
                return;
            }
            else {
                throw SyntaxError(SyntaxError::missing_value, opt);
            }
        }
    }
    void handleLongOpt(std::string_view optName) {
        auto         opt = optName.substr(0, optName.find('='));
        auto         val = opt.length() < optName.length() ? optName.substr(opt.length() + 1) : std::string_view{};
        SharedOptPtr fallback;
        bool         flagVal = (flags_ & static_cast<unsigned>(command_line_allow_flag_value)) != 0u;
        if (val.empty() && optName.starts_with("no-")) {
            if (auto no = getOpt(optName.substr(3), [](auto) { return SharedOptPtr(); });
                no && no->value()->isNegatable()) {
                fallback = std::move(no);
            }
        }
        auto o = getOpt(opt, [&](ContextError::Type t) {
            if (t == ContextError::unknown_option && fallback) {
                flagVal = true;
                val     = "no";
                return fallback;
            }
            throw;
        });
        if (val.empty() && not o->value()->isImplicit() && (val = next()).empty()) { // NOLINT
            throw SyntaxError(SyntaxError::missing_value, opt);
        }
        if (not val.empty() && not flagVal && o->value()->isFlag()) { // flags don't have values
            throw SyntaxError(SyntaxError::extra_value, opt);
        }
        applyValue(o, val);
    }
    void handlePositionalOpt(std::string_view tok) {
        std::string name;
        if (not posOpt_ || not posOpt_(tok, name)) {
            name = "Positional Option";
        }
        applyValue(getOption(name, OptionContext::find_name_or_prefix), tok);
    }
    PosOption posOpt_;
    unsigned  flags_;
};

class ArgvParser : public CommandLineParser {
public:
    ArgvParser(ParseContext& ctx, std::span<const char* const> argv, PosOption posOpt, unsigned cmdFlags)
        : CommandLineParser(ctx, std::move(posOpt), cmdFlags)
        , argv_(argv) {}

private:
    std::string_view next() override {
        if (not argv_.empty()) {
            std::string_view r(argv_.front());
            argv_ = argv_.subspan(1);
            return r; // NOLINT
        }
        return {};
    }
    std::span<const char* const> argv_;
};

class CommandStringParser : public CommandLineParser {
public:
    CommandStringParser(ParseContext& ctx, std::string_view cmd, PosOption posOpt, unsigned cmdFlags)
        : CommandLineParser(ctx, std::move(posOpt), cmdFlags)
        , cmd_(cmd) {
        tok_.reserve(80);
    }
    CommandStringParser& operator=(const CommandStringParser&) = delete;

private:
    std::string_view next() override {
        // skip leading white
        while (not cmd_.empty() && std::isspace(static_cast<unsigned char>(cmd_.front()))) { cmd_.remove_prefix(1); }
        if (cmd_.empty()) {
            return {};
        }
        tok_.clear();
        static constexpr std::string_view special{"\"'\\"};
        // find the end of the current arg
        for (char c, t = ' '; not cmd_.empty(); cmd_.remove_prefix(1)) {
            if (c = cmd_.front(); c == t) {
                if (t == ' ') {
                    break;
                }
                t = ' ';
            }
            else if ((c == '\'' || c == '"') && t == ' ') {
                t = c;
            }
            else if (c != '\\' || special.find(cmd_[cmd_.size() > 1]) == std::string_view::npos) {
                tok_ += c;
            }
            else if (cmd_.size() > 1) {
                tok_ += cmd_[1];
                cmd_.remove_prefix(1);
            }
        }
        return tok_;
    }
    std::string_view cmd_;
    std::string      tok_;
};
///////////////////////////////////////////////////////////////////////////////
// class CfgFileParser
///////////////////////////////////////////////////////////////////////////////
class CfgFileParser : public OptionParser {
public:
    CfgFileParser(ParseContext& ctx, std::istream& in) : OptionParser(ctx), in_(in) {}
    void operator=(const CfgFileParser&) = delete;

private:
    static void trimLeft(std::string& str, const std::string& charList = " \t") {
        if (auto pos = str.find_first_not_of(charList); pos != 0) {
            str.erase(0, pos);
        }
    }
    static void trimRight(std::string& str, const std::string& charList = " \t") {
        if (auto pos = str.find_last_not_of(charList); pos != std::string::npos) {
            str.erase(pos + 1, std::string::npos);
        }
    }
    static bool splitHalf(const std::string& str, const std::string& seperator, std::string& leftSide,
                          std::string& rightSide) {
        auto sepPos = str.find(seperator);
        leftSide.assign(str, 0, sepPos);
        if (sepPos != std::string::npos) {
            rightSide.assign(str, sepPos + seperator.length(), std::string::npos);
            return true;
        }
        return false;
    }
    void doParse() override {
        [[maybe_unused]] auto lineNr = 0;

        std::string sectionName;       // current section name
        std::string sectionValue;      // current section value
        bool        inSection = false; // true if multi line section value
        FindType    ft        = OptionContext::find_name_or_prefix;
        // Reads the config file.
        // A config file may only contain empty lines, single line comments or
        // sections structured in a name = value fashion.
        // Value can span multiple lines, but parts in different lines than name
        // must not contain a `=`-Character.
        for (std::string line; std::getline(in_, line);) {
            ++lineNr;
            trimLeft(line);
            trimRight(line);

            if (line.empty() || line.starts_with('#')) {
                // An empty line or single line comment stops a multi-line section value.
                if (inSection) {
                    if (auto opt = getOption(sectionName, ft); opt.get()) {
                        applyValue(opt, sectionValue);
                    }
                    inSection = false;
                }
                continue;
            }

            if (auto pos = line.find('='); pos != std::string::npos) {
                // A new section terminates a multi-line section value.
                // First process the current section value...
                if (auto opt = inSection ? getOption(sectionName, ft) : SharedOptPtr{}; opt.get()) {
                    applyValue(opt, sectionValue);
                }
                // ...then save the new section's value.
                splitHalf(line, "=", sectionName, sectionValue);
                trimRight(sectionName);
                trimLeft(sectionValue, " \t\n");
                inSection = true;
            }
            else if (inSection) {
                sectionValue += " ";
                sectionValue += line;
            }
            else {
                throw SyntaxError(SyntaxError::invalid_format, line);
            }
        }
        if (inSection) { // config file does not end with an empty line
            if (auto opt = getOption(sectionName, ft); opt.get()) {
                applyValue(opt, sectionValue);
            }
        }
    }
    std::istream& in_;
};
} // end unnamed namespace
ParseContext& parseCommandArray(ParseContext& ctx, std::span<const char* const> args, PosOption pos, unsigned flags) {
    return ArgvParser(ctx, args, std::move(pos), flags).parse();
}
ParseContext& parseCommandString(ParseContext& ctx, std::string_view args, PosOption pos, unsigned flags) {
    return CommandStringParser(ctx, args, std::move(pos), flags).parse();
}
ParseContext& parseCfgFile(ParseContext& ctx, std::istream& is) { return CfgFileParser(ctx, is).parse(); }
///////////////////////////////////////////////////////////////////////////////
// Errors
///////////////////////////////////////////////////////////////////////////////
static std::string format(SyntaxError::Type t, std::string_view key) {
    return std::string("SyntaxError: "sv).append(quote(key)).append([](SyntaxError::Type type) {
        switch (type) {
            case SyntaxError::missing_value : return " requires a value!"sv;
            case SyntaxError::extra_value   : return " does not take a value!"sv;
            case SyntaxError::invalid_format: return " unrecognized line!"sv;
            default                         : return " unknown syntax!"sv;
        }
    }(t));
}
static std::string formatContext(std::string_view ctx) {
    std::string ret;
    if (not ctx.empty()) {
        ret.append("In context "sv).append(quote(ctx)).append(": "sv);
    }
    return ret;
}
static std::string format(ContextError::Type t, std::string_view ctx, std::string_view key, std::string_view alt) {
    std::string ret = formatContext(ctx);
    switch (t) {
        case ContextError::duplicate_option: ret.append("duplicate option: "sv); break;
        case ContextError::unknown_option  : ret.append("unknown option: "sv); break;
        case ContextError::ambiguous_option: ret.append("ambiguous option: "sv); break;
        case ContextError::unknown_group   : ret.append("unknown group: "sv); break;
        default                            : ret.append("unknown error in: "sv);
    }
    ret.append(quote(key));
    if (t == ContextError::ambiguous_option && not alt.empty()) {
        ret.append(" could be:\n"sv).append(alt);
    }
    return ret;
}
static std::string format(ValueError::Type t, std::string_view ctx, std::string_view key, std::string_view value) {
    std::string ret = formatContext(ctx);
    switch (std::string_view x; t) {
        case ValueError::multiple_occurrences: ret.append("multiple occurrences: "sv); break;
        case ValueError::invalid_default     : x = "default "sv; [[fallthrough]];
        case ValueError::invalid_value:
            ret.append(quote(value)).append(" invalid "sv).append(x).append("value for: "sv);
            break;
        default: ret.append("unknown error in: "sv);
    }
    ret.append(quote(key));
    return ret;
}
SyntaxError::SyntaxError(Type t, std::string_view key) : Error(format(t, key)), key_(key), type_(t) {}
ContextError::ContextError(std::string_view ctx, Type t, std::string_view key, std::string_view alt)
    : Error(format(t, ctx, key, alt))
    , ctx_(ctx)
    , key_(key)
    , type_(t) {}
ValueError::ValueError(std::string_view ctx, Type t, std::string_view opt, std::string_view value)
    : Error(format(t, ctx, opt, value))
    , ctx_(ctx)
    , key_(opt)
    , value_(value)
    , type_(t) {}
} // namespace Potassco::ProgramOptions
