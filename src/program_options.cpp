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
#include <utility>
using namespace std;

namespace Potassco::ProgramOptions {
///////////////////////////////////////////////////////////////////////////////
// DefaultFormat
///////////////////////////////////////////////////////////////////////////////
std::size_t DefaultFormat::format(std::string& buffer, const Option& o, std::size_t maxW) {
    auto        bufSize = std::max(maxW, o.maxColumn()) + 3;
    const auto* arg     = o.argName();
    auto        np      = ""sv;
    auto        ap      = ""sv;
    if (o.value()->isNegatable()) {
        if (not *arg) {
            np = "[no-]"sv;
        }
        else {
            ap       = "|no"sv;
            bufSize += ap.size();
        }
    }
    const auto startSize = buffer.size();
    buffer.reserve(startSize + bufSize);
    buffer.append("  --"sv).append(np).append(o.name());
    if (o.value()->isImplicit() && *arg) {
        buffer.append("[="sv).append(arg).append(ap).append("]"sv);
    }
    if (auto c = o.alias(); c) {
        buffer.append(",-"sv).append(1, c);
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
const char* Str::clone(Str source) {
    auto tmp = std::make_unique<char[]>(std::strlen(source.c_str()) + 1);
    std::strcpy(tmp.get(), source.c_str());
    return tmp.release();
}
///////////////////////////////////////////////////////////////////////////////
// class Value
///////////////////////////////////////////////////////////////////////////////
Value::Value(State initial) : state_(static_cast<uint8_t>(initial)) {
    static_assert(sizeof(Value) == sizeof(void*) * 3, "unexpected size");
}

Value::~Value() {
    for (auto x : {desc_name, desc_default, desc_implicit}) {
        if (test_bit(own_, x)) {
            delete[] desc(x);
        }
    }
    if (descVal_ == desc_pack) {
        ::operator delete(static_cast<void*>(desc_.pack));
    }
}

const char* Value::arg() const {
    if (const char* x = desc(desc_name)) {
        return x;
    }
    return isFlag() ? "" : "<arg>";
}

Value* Value::desc(DescType t, Str s) {
    if (t == desc_implicit) {
        implicit_ = 1;
        if (s.empty()) {
            return this;
        }
    }
    if (descVal_ == t || (descVal_ == 0 && desc_.value == nullptr)) {
        copyOrBorrow(&desc_.value, s, t);
        descVal_ = static_cast<uint8_t>(t);
        return this;
    }
    if (descVal_ != desc_pack) {
        auto* pack = static_cast<const char**>(::operator new(sizeof(const char* [3])));
        pack[0] = pack[1] = pack[2] = nullptr;
        pack[descVal_]              = desc_.value;
        desc_.pack                  = pack;
        descVal_                    = desc_pack;
    }
    copyOrBorrow(&desc_.pack[t], s, t);
    return this;
}
void Value::copyOrBorrow(const char** target, Str source, DescType t) {
    if (test_bit(own_, t)) {
        delete[] *target;
    }
    if (source.isLit()) {
        *target = source.c_str();
        own_    = clear_bit(own_, t);
    }
    else {
        *target = Str::clone(source);
        own_    = set_bit(own_, t);
    }
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

bool Value::parse(std::string_view name, std::string_view value, State st) {
    if (not value.empty() || not isImplicit()) {
        return state(doParse(name, value), st);
    }
    const char* x = implicit();
    assert(x);
    return state(doParse(name, x), st);
}
///////////////////////////////////////////////////////////////////////////////
// class Option
///////////////////////////////////////////////////////////////////////////////
Option::Option(std::string_view longName, char alias, Str desc, Value* v)
    : name_(longName)
    , description_(desc.c_str())
    , value_(v) {
    assert(v);
    assert(not longName.empty());
    value_->alias(alias);
    if (not desc.isLit()) {
        description_ = Str::clone(desc);
        value_->rtDesc(true);
    }
}
Option::~Option() {
    if (value_ && value_->rtDesc()) {
        delete[] description_;
    }
}
Option::Option(Option&& opt) noexcept
    : name_(std::move(opt.name_))
    , description_(std::exchange(opt.description_, nullptr))
    , value_(std::move(opt.value_)) {}

Option& Option::operator=(Option&& rhs) noexcept {
    if (this != &rhs) {
        Option t(std::move(*this));
        name_        = std::move(rhs.name_);
        description_ = std::exchange(rhs.description_, nullptr);
        value_       = std::move(rhs.value_);
    }
    return *this;
}

std::size_t Option::maxColumn() const {
    std::size_t col = 4 + name_.size(); //  --name
    if (alias()) {
        col += 3; // ,-o
    }
    if (std::size_t argN = strlen(argName())) {
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
    if (value()->defaultsTo() != nullptr && value()->state() != Value::value_defaulted) {
        return value()->parse(name(), value()->defaultsTo(), Value::value_defaulted);
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

void OptionGroup::format(OptionOutput& out, size_t maxW, DescriptionLevel dl) const {
    for (const auto& opt : options_) {
        if (opt->descLevel() <= dl) {
            out.printOption(*opt, maxW);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////
// class OptionInitHelper
///////////////////////////////////////////////////////////////////////////////
OptionInitHelper::OptionInitHelper(OptionGroup& owner) : owner_(&owner) {}

OptionInitHelper& OptionInitHelper::operator()(const char* name, Value* val, Str desc) {
    std::unique_ptr<Value> cleanup(val);
    if (not name || not *name || *name == ',' || *name == '!') {
        throw Error("Invalid empty option name");
    }
    const char* n = strchr(name, ',');
    string      longName;
    char        shortName = 0;
    if (not n) {
        longName = name;
    }
    else {
        longName.assign(name, n);
        unsigned    level = owner_->descLevel();
        const char* x     = ++n;
        if (*x && (not x[1] || x[1] == ',')) {
            shortName  = *x++;
            x         += *x == ',';
        }
        if (*x == '@') {
            ++x;
            level = 0;
            while (*x >= '0' && *x <= '9') {
                level *= 10;
                level += static_cast<unsigned>(*x - '0');
                ++x;
            }
        }
        if (not *n || *x || level > desc_level_hidden) {
            throw Error(std::string("Invalid Key '").append(name).append("'"));
        }
        val->level(static_cast<DescriptionLevel>(level));
    }
    if (*(longName.end() - 1) == '!') {
        bool neg = *(longName.end() - 2) != '\\';
        longName.erase(longName.end() - (1 + not neg), longName.end());
        if (neg) {
            val->negatable();
        }
        else {
            longName += '!';
        }
    }
    owner_->addOption(SharedOptPtr(new Option(longName, shortName, desc, val)));
    std::ignore = cleanup.release();
    return *this;
}
///////////////////////////////////////////////////////////////////////////////
// class OptionContext
///////////////////////////////////////////////////////////////////////////////
OptionContext::OptionContext(std::string_view caption, DescriptionLevel def) : caption_(caption), descLevel_(def) {}

auto   OptionContext::caption() const -> const std::string& { return caption_; }
void   OptionContext::setActiveDescLevel(DescriptionLevel level) { descLevel_ = std::min(level, desc_level_all); }
size_t OptionContext::findGroupKey(std::string_view name) const {
    for (size_t i = 0; i != groups_.size(); ++i) {
        if (groups_[i].caption() == name) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

OptionContext& OptionContext::add(const OptionGroup& group) {
    size_t k = findGroupKey(group.caption());
    if (k >= groups_.size()) {
        // add as a new group
        k = groups_.size();
        groups_.emplace_back(group.caption(), group.descLevel());
    }
    for (const auto& opt : group) { insertOption(k, opt); }
    groups_[k].setDescriptionLevel(std::min(group.descLevel(), groups_[k].descLevel()));
    return *this;
}

OptionContext& OptionContext::addAlias(std::string aliasName, option_iterator option) {
    if (option != end() && not aliasName.empty()) {
        auto k         = static_cast<KeyType>(option - begin());
        auto [it, ins] = index_.try_emplace(std::move(aliasName), k);
        if (not ins) {
            throw DuplicateOption(caption(), it->first);
        }
    }
    return *this;
}

const OptionGroup& OptionContext::findGroup(std::string_view name) const {
    if (std::size_t x = findGroupKey(name); x < groups_.size()) {
        return groups_[x];
    }
    throw ContextError(caption(), ContextError::unknown_group, name);
}
const OptionGroup* OptionContext::tryFindGroup(std::string_view name) const {
    std::size_t x = findGroupKey(name);
    return x < groups_.size() ? &groups_[x] : nullptr;
}

OptionContext& OptionContext::add(const OptionContext& other) {
    if (this == &other) {
        return *this;
    }
    for (const auto& grp : other.groups_) { add(grp); }
    return *this;
}

void OptionContext::insertOption(size_t groupId, const SharedOptPtr& opt) {
    const string& l = opt->name();
    KeyType       k(options_.size());
    if (opt->alias()) {
        if (char sName[2] = {'-', opt->alias()}; not index_.try_emplace({sName, 2}, k).second) {
            throw DuplicateOption(caption(), l);
        }
    }
    if (not l.empty() && not index_.try_emplace(l, k).second) {
        throw DuplicateOption(caption(), l);
    }
    options_.push_back(opt);
    groups_[groupId].options_.push_back(opt);
}

OptionContext::option_iterator OptionContext::find(std::string_view key, FindType t) const {
    return options_.begin() + static_cast<std::ptrdiff_t>(findImpl(key, t, static_cast<unsigned>(-1)).first->second);
}

OptionContext::option_iterator OptionContext::tryFind(std::string_view key, FindType t) const {
    PrefixRange r = findImpl(key, t, 0u);
    return std::distance(r.first, r.second) == 1 ? options_.begin() + static_cast<std::ptrdiff_t>(r.first->second)
                                                 : end();
}

OptionContext::PrefixRange OptionContext::findImpl(std::string_view key, FindType t, unsigned eMask,
                                                   std::string_view eCtx) const {
    std::string alias;
    if (t == find_alias && not key.starts_with('-')) {
        alias.append(1, '-').append(key);
        key = alias;
    }
    auto it = index_.lower_bound(key);
    auto up = it;
    if (it != index_.end()) {
        if ((it->first == key) && ((t & (find_alias | find_name)) != 0)) {
            ++up;
        }
        else if ((t & find_prefix) != 0) {
            while (up->first.starts_with(key) && ++up != index_.end()) {}
        }
    }
    if (std::distance(it, up) != 1 && eMask) {
        if ((eMask & 1u) && it == up) {
            throw UnknownOption(eCtx, key);
        }
        if ((eMask & 2u) && it != up) {
            std::string str;
            for (; it != up; ++it) {
                str += "  ";
                str += it->first;
                str += "\n";
            }
            throw AmbiguousOption(eCtx, key, str);
        }
    }
    return {it, up};
}

OptionOutput& OptionContext::description(OptionOutput& out) const {
    DescriptionLevel dl = descLevel_;
    if (out.printContext(*this)) {
        size_t maxW = 23;
        for (const auto& grp : groups_) { maxW = std::max(maxW, grp.maxColumn(dl)); }
        // print all visible groups
        for (std::size_t i = 1; i < groups_.size(); ++i) {
            if (groups_[i].descLevel() <= dl && out.printGroup(groups_[i])) {
                groups_[i].format(out, maxW, dl);
            }
        }
        if (not groups_.empty() && groups_[0].descLevel() <= dl && out.printGroup(groups_[0])) {
            groups_[0].format(out, maxW, dl);
        }
    }
    return out;
}

std::string OptionContext::defaults(std::size_t n) const {
    DescriptionLevel dl   = descLevel_;
    std::size_t      line = n;
    std::string      defs;
    defs.reserve(options_.size());
    std::string opt;
    opt.reserve(80);
    for (int g = 0; g < 2; ++g) {
        // print all subgroups followed by the main group
        for (std::size_t i = (g == 0), end = (g == 0) ? groups_.size() : 1; i < end; ++i) {
            if (groups_[i].descLevel() <= dl) {
                for (const auto& optPtr : groups_[i]) {
                    const Option& o = *optPtr;
                    if (o.value()->defaultsTo() && o.descLevel() <= dl) {
                        ((((opt += "--") += o.name()) += "=") += o.value()->defaultsTo());
                        if (line + opt.size() > 78) {
                            defs += '\n';
                            defs.append(n, ' ');
                            line = n;
                        }
                        defs += opt;
                        defs += ' ';
                        line += opt.size() + 1;
                        opt.clear();
                    }
                }
            }
        }
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
bool ParsedOptions::assign(const ParsedValues& p, const ParsedOptions* exclude) {
    if (not p.ctx) {
        return false;
    }
    struct Assign {
        Assign(ParsedOptions* x, const ParsedOptions* exclude) : self(x), ignore(exclude) {}
        void assign(const ParsedValues& p) {
            begin = it = p.begin();
            // assign parsed values
            for (auto end = p.end(); it != end; ++it) {
                const Option& o = *it->first;
                if (ignore && ignore->contains(o.name()) && not o.value()->isComposing()) {
                    continue;
                }
                if (int ret = self->assign(o, it->second)) {
                    throw ValueError(p.ctx->caption(), static_cast<ValueError::Type>(ret - 1), o.name(), it->second);
                }
            }
        }
        ~Assign() {
            for (auto x = begin, end = this->it; x != end; ++x) {
                const Option& o = *x->first;
                assert(o.value()->state() == Value::value_fixed || self->parsed_.contains(o.name()) ||
                       ignore->contains(o.name()));
                if (o.value()->state() == Value::value_fixed) {
                    self->parsed_.insert(o.name());
                    o.value()->state(Value::value_unassigned);
                }
            }
        }
        ParsedOptions*         self;
        const ParsedOptions*   ignore;
        ParsedValues::iterator begin;
        ParsedValues::iterator it;
    } scoped(this, exclude);
    scoped.assign(p);
    return true;
}
int ParsedOptions::assign(const Option& o, const std::string& value) {
    unsigned badState = 0;
    if (not o.value()->isComposing()) {
        if (parsed_.contains(o.name())) {
            return 0;
        }
        badState = (Value::value_fixed & o.value()->state());
    }
    if (badState || not o.value()->parse(o.name(), value, Value::value_fixed)) {
        return badState ? 1 + ValueError::multiple_occurrences : 1 + ValueError::invalid_value;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////
// class ParsedValues
///////////////////////////////////////////////////////////////////////////////
void ParsedValues::add(std::string_view name, std::string_view value) {
    if (auto it = ctx->tryFind(name); it != ctx->end()) {
        add(*it, value);
    }
}
///////////////////////////////////////////////////////////////////////////////
// class OptionParser
///////////////////////////////////////////////////////////////////////////////
OptionParser::OptionParser(ParseContext& o) : ctx_(&o) {}

OptionParser::~OptionParser() = default;

ParseContext& OptionParser::parse() {
    doParse();
    return *ctx_;
}
ParseContext::~ParseContext() = default;
namespace {
///////////////////////////////////////////////////////////////////////////////
// class CommandLineParser
///////////////////////////////////////////////////////////////////////////////
class CommandLineParser : public OptionParser {
public:
    enum OptionType { short_opt, long_opt, end_opt, no_opt };
    CommandLineParser(ParseContext& ctx, unsigned f) : OptionParser(ctx), flags(f) {}
    unsigned flags;
    unsigned consumed{0};

private:
    virtual std::string_view next() = 0;
    void                     doParse() override {
        bool breakEarly = false;
        int  posKey     = 0;
        for (std::string_view curr; not(curr = next()).empty() && not breakEarly;) {
            switch (getOptionType(curr)) {
                case short_opt:
                    if (handleShortOpt(curr.substr(1))) {
                        curr = {};
                    }
                    break;
                case long_opt:
                    if (handleLongOpt(curr.substr(2))) {
                        curr = {};
                    }
                    break;
                case end_opt:
                    curr       = {};
                    breakEarly = true;
                    break;
                case no_opt: {
                    SharedOptPtr opt = getOption(posKey++, curr);
                    if (opt.get()) {
                        addOptionValue(opt, curr);
                        curr = {};
                    }
                    break;
                }
                default: assert(0);
            }
            if (curr.empty()) {
                ++consumed;
            }
        }
    }
    static OptionType getOptionType(std::string_view o) {
        if (o.starts_with("--")) {
            return o.size() > 2 ? long_opt : end_opt;
        }
        return o.starts_with('-') && o.size() > 1 ? short_opt : no_opt;
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
    bool handleShortOpt(std::string_view optName) {
        // either -o value or -o[value|opts]
        char             tmp[2] = {'-', 0};
        std::string_view opt(tmp, 2);
        while (not optName.empty()) {
            tmp[1]   = optName.front();
            auto val = optName.substr(1);
            if (auto o = getOption(opt, OptionContext::find_alias); o.get()) {
                if (o->value()->isImplicit()) {
                    // -ovalue or -oopts
                    if (not o->value()->isFlag()) {
                        // consume (possibly empty) value
                        addOptionValue(o, val);
                        return true;
                    }
                    // -o + more options
                    addOptionValue(o, {});
                    optName.remove_prefix(1);
                }
                else if (val = val.empty() ? next() : val; not val.empty()) {
                    //  -ovalue or -o value
                    addOptionValue(o, val);
                    return true;
                }
                else {
                    throw SyntaxError(SyntaxError::missing_value, opt);
                }
            }
            else {
                return false;
            }
        }
        return true;
    }
    bool handleLongOpt(std::string_view optName) {
        auto         opt = optName.substr(0, optName.find('='));
        auto         val = opt.length() < optName.length() ? optName.substr(opt.length() + 1) : std::string_view{};
        SharedOptPtr fallback;
        bool         flagVal = (flags & static_cast<unsigned>(command_line_allow_flag_value)) != 0u;
        if (val.empty() && optName.starts_with("no-")) {
            if (auto no = getOpt(optName.substr(3), [](auto) { return SharedOptPtr(); });
                no && no->value()->isNegatable()) {
                fallback = std::move(no);
            }
        }
        if (auto o = getOpt(opt,
                            [&](ContextError::Type t) {
                                if (t == ContextError::unknown_option && fallback) {
                                    flagVal = true;
                                    val     = "no";
                                    return fallback;
                                }
                                throw;
                            });
            o) {
            if (val.empty() && not o->value()->isImplicit() && (val = next()).empty()) { // NOLINT
                throw SyntaxError(SyntaxError::missing_value, opt);
            }
            if (not val.empty() && not flagVal && o->value()->isFlag()) { // flags don't have values
                throw SyntaxError(SyntaxError::extra_value, opt);
            }
            addOptionValue(o, val);
            return true;
        }
        return false;
    }
};

class ArgvParser : public CommandLineParser {
public:
    ArgvParser(ParseContext& ctx, std::span<const char* const> argv, unsigned cmdFlags)
        : CommandLineParser(ctx, cmdFlags)
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
    CommandStringParser(std::string_view cmd, ParseContext& ctx, unsigned cmdFlags)
        : CommandLineParser(ctx, cmdFlags)
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
        std::string::size_type pos = str.find_first_not_of(charList);
        if (pos != 0) {
            str.erase(0, pos);
        }
    }
    static void trimRight(std::string& str, const std::string& charList = " \t") {
        std::string::size_type pos = str.find_last_not_of(charList);
        if (pos != std::string::npos) {
            str.erase(pos + 1, std::string::npos);
        }
    }
    static bool splitHalf(const std::string& str, const std::string& seperator, std::string& leftSide,
                          std::string& rightSide) {
        std::string::size_type sepPos = str.find(seperator);
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
                        addOptionValue(opt, sectionValue);
                    }
                    inSection = false;
                }
                continue;
            }

            if (auto pos = line.find('='); pos != std::string::npos) {
                // A new section terminates a multi-line section value.
                // First process the current section value...
                if (auto opt = inSection ? getOption(sectionName, ft) : SharedOptPtr{}; opt.get()) {
                    addOptionValue(opt, sectionValue);
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
                addOptionValue(opt, sectionValue);
            }
        }
    }
    std::istream& in_;
};
class DefaultContext : public ParseContext {
public:
    DefaultContext(const OptionContext& o, bool allowUnreg, PosOption po)
        : posOpt(std::move(po))
        , parsed(o)
        , eMask(2u + static_cast<unsigned>(not allowUnreg)) {}
    SharedOptPtr getOption(std::string_view name, FindType ft) override {
        if (auto r = parsed.ctx->findImpl(name, ft, eMask); r.first != r.second) {
            return *(parsed.ctx->begin() + static_cast<std::ptrdiff_t>(r.first->second));
        }
        return SharedOptPtr(nullptr);
    }
    SharedOptPtr getOption(int, std::string_view tok) override {
        std::string optName;
        if (not posOpt || not posOpt(tok, optName)) {
            return getOption("Positional Option", OptionContext::find_name_or_prefix);
        }
        return getOption(optName, OptionContext::find_name_or_prefix);
    }
    void         addValue(const SharedOptPtr& key, std::string_view value) override { parsed.add(key, value); }
    PosOption    posOpt;
    ParsedValues parsed;
    unsigned     eMask;
};

} // end unnamed namespace
ParsedValues parseCommandArray(std::span<const char* const> args, const OptionContext& ctx, PosOption posParser,
                               unsigned flags, unsigned* consumed) {
    DefaultContext parseCtx(ctx, (flags & command_line_allow_unregistered) != 0, std::move(posParser));
    ArgvParser     parser(parseCtx, args, flags);
    parser.parse();
    if (consumed) {
        *consumed = parser.consumed;
    }
    return parseCtx.parsed;
}
ParseContext& parseCommandString(std::string_view cmd, ParseContext& ctx, unsigned flags) {
    return CommandStringParser(cmd, ctx, flags).parse();
}
ParsedValues parseCommandString(std::string_view cmd, const OptionContext& ctx, PosOption posParser, unsigned flags) {
    DefaultContext parseCtx(ctx, (flags & command_line_allow_unregistered) != 0, std::move(posParser));
    CommandStringParser(cmd, parseCtx, flags).parse();
    return parseCtx.parsed;
}

ParsedValues parseCfgFile(std::istream& in, const OptionContext& o, bool allowUnreg) {
    DefaultContext parseCtx(o, allowUnreg, nullptr);
    CfgFileParser(parseCtx, in).parse();
    return parseCtx.parsed;
}

///////////////////////////////////////////////////////////////////////////////
// Errors
///////////////////////////////////////////////////////////////////////////////
static std::string quote(std::string_view x) { return std::string(1, '\'').append(x).append(1, '\''); }
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
