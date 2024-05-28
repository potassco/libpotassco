//
// Copyright (c) 2016 - present, Benjamin Kaufmann
//
// This file is part of Potassco.
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
#include <potassco/aspif_text.h>

#include <potassco/error.h>
#include <potassco/rule_utils.h>
POTASSCO_WARNING_BEGIN_RELAXED
#include <amc/vector.hpp>
POTASSCO_WARNING_END_RELAXED

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <ostream>
#include <sstream>
#include <unordered_map>

namespace Potassco {
using namespace std::literals;
static bool isLower(char c) { return std::islower(static_cast<unsigned char>(c)); }
static bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)); }
struct AspifTextInput::Data {
    void clear() {
        rule.clear();
        symbol.clear();
    }
    [[nodiscard]] AtomSpan atoms() const { return rule.head(); }
    [[nodiscard]] LitSpan  lits() const { return rule.body(); }

    RuleBuilder   rule;
    DynamicBuffer symbol;
};
AspifTextInput::AspifTextInput(AbstractProgram* out) : out_(out), data_(nullptr) {}
void AspifTextInput::setOutput(AbstractProgram& out) { out_ = &out; }
bool AspifTextInput::doAttach(bool& inc) {
    auto n = peek();
    if (out_ && (not n || isLower(n) || std::strchr(".#%{:", n))) {
        while (n == '%') {
            skipLine();
            n = skipWs();
        }
        if (inc = matchOpt("#incremental"sv); inc)
            matchDelim('.');
        out_->initProgram(inc);
        return true;
    }
    return false;
}

bool AspifTextInput::doParse() {
    out_->beginStep();
    parseStatements();
    out_->endStep();
    return true;
}

void AspifTextInput::parseStatements() {
    require(out_ != nullptr, "output not set");
    Data data;
    POTASSCO_SCOPE_EXIT({ data_ = nullptr; });
    data_ = &data;
    for (char c; (c = skipWs()) != 0; data.clear()) {
        if (c == '.') {
            matchDelim('.');
        }
        else if (c == '#') {
            if (not matchDirective())
                break;
        }
        else if (c == '%') {
            skipLine();
        }
        else {
            matchRule(c);
        }
    }
}

void AspifTextInput::matchRule(char c) {
    if (c == '{') {
        matchDelim('{');
        data_->rule.start(Head_t::Choice);
        matchAtoms(";,"sv);
        matchDelim('}');
    }
    else {
        data_->rule.start();
        matchAtoms(";|"sv);
    }
    if (matchOpt(":-"sv)) {
        c = skipWs();
        if (not StreamType::isDigit(c) && c != '-') {
            data_->rule.startBody();
            matchLits();
        }
        else {
            data_->rule.startSum(matchInt());
            matchAgg();
        }
    }
    matchDelim('.');
    data_->rule.end(out_);
}

bool AspifTextInput::matchDirective() {
    if (matchOpt("#minimize"sv)) {
        data_->rule.startMinimize(0);
        matchAgg();
        Weight_t prio = matchOpt("@"sv) ? matchInt() : 0;
        matchDelim('.');
        data_->rule.setBound(prio);
        data_->rule.end(out_);
    }
    else if (matchOpt("#project"sv)) {
        data_->rule.start();
        if (matchOpt("{"sv)) {
            matchAtoms(","sv);
            matchDelim('}');
        }
        matchDelim('.');
        out_->project(data_->atoms());
    }
    else if (matchOpt("#output"sv)) {
        matchTerm();
        matchCondition();
        matchDelim('.');
        out_->output(data_->symbol.view(), data_->lits());
    }
    else if (matchOpt("#external"sv)) {
        auto a = matchId();
        auto v = Value_t::False;
        matchDelim('.');
        if (matchOpt("["sv)) {
            if (matchOpt("true"sv)) {
                v = Value_t::True;
            }
            else if (matchOpt("free"sv)) {
                v = Value_t::Free;
            }
            else if (matchOpt("release"sv)) {
                v = Value_t::Release;
            }
            else {
                require(matchOpt("false"sv), "<value> expected");
            }
            matchDelim(']');
        }
        out_->external(a, v);
    }
    else if (matchOpt("#assume"sv)) {
        data_->rule.startBody();
        if (matchOpt("{"sv)) {
            matchLits();
            matchDelim('}');
        }
        matchDelim('.');
        out_->assume(data_->lits());
    }
    else if (matchOpt("#heuristic"sv)) {
        auto a = matchId();
        matchCondition();
        matchDelim('.');
        matchDelim('[');
        auto v = matchInt();
        auto p = 0;
        if (matchOpt("@"sv)) {
            p = matchInt();
            require(p >= 0, "positive priority expected");
        }
        matchDelim(',');
        auto ht = matchHeuMod();
        matchDelim(']');
        out_->heuristic(a, ht, v, static_cast<unsigned>(p), data_->lits());
    }
    else if (matchOpt("#edge"sv)) {
        int s, t;
        matchDelim('('), s = matchInt(), matchDelim(','), t = matchInt(), matchDelim(')');
        matchCondition();
        matchDelim('.');
        out_->acycEdge(s, t, data_->lits());
    }
    else if (matchOpt("#step"sv)) {
        require(incremental(), "#step requires incremental program");
        matchDelim('.');
        return false;
    }
    else if (matchOpt("#incremental"sv)) {
        matchDelim('.');
    }
    else {
        error("unrecognized directive");
    }
    return true;
}
bool AspifTextInput::matchOpt(std::string_view term) {
    if (match(term)) {
        skipWs();
        return true;
    }
    return false;
}

void AspifTextInput::matchDelim(char c) {
    matchChar(c);
    skipWs();
}

void AspifTextInput::matchAtoms(std::string_view seps) {
    if (isLower(skipWs())) {
        do {
            auto x = matchLit();
            require(x > 0, "positive atom expected");
            data_->rule.addHead(static_cast<Atom_t>(x));
        } while (seps.find(peek()) != std::string_view::npos && get() && (skipWs(), true));
    }
}
void AspifTextInput::matchLits() {
    if (isLower(skipWs())) {
        do { data_->rule.addGoal(matchLit()); } while (matchOpt(","sv));
    }
}
void AspifTextInput::matchCondition() {
    data_->rule.startBody();
    if (matchOpt(":"sv)) {
        matchLits();
    }
}
void AspifTextInput::matchAgg() {
    matchDelim('{');
    if (not matchOpt("}"sv)) {
        do {
            auto wl = WeightLit_t{matchLit(), 1};
            if (matchOpt("="sv)) {
                wl.weight = matchInt();
            }
            data_->rule.addGoal(wl);
        } while (matchOpt(","sv));
        matchDelim('}');
    }
}

Lit_t AspifTextInput::matchLit() {
    int s = matchOpt("not "sv) ? -1 : 1;
    return static_cast<Lit_t>(matchId()) * s;
}

int AspifTextInput::matchInt() {
    auto i = ProgramReader::matchInt();
    skipWs();
    return i;
}
Atom_t AspifTextInput::matchId() {
    auto c = get();
    auto n = peek();
    require(isLower(c), "<id> expected");
    require(not isLower(n), "<pos-integer> expected");
    if (c == 'x' && (BufferedStream::isDigit(n) || n == '_')) {
        if (n == '_') {
            get();
        }
        auto i = matchInt();
        require(i > 0, "<pos-integer> expected");
        return static_cast<Atom_t>(i);
    }
    else {
        skipWs();
        return static_cast<Atom_t>(c - 'a') + 1;
    }
}
void AspifTextInput::push(char c) { data_->symbol.push(c); }

void AspifTextInput::matchTerm() {
    auto c = peek();
    if (isLower(c) || c == '_') {
        do { push(get()); } while (isAlnum(c = peek()) != 0 || c == '_');
        skipWs();
        if (matchOpt("("sv)) {
            push('(');
            for (;;) {
                matchAtomArg();
                if (not matchOpt(","sv))
                    break;
                push(',');
            }
            matchDelim(')');
            push(')');
        }
    }
    else if (c == '"') {
        matchStr();
    }
    else {
        error("<term> expected");
    }
    skipWs();
}
void AspifTextInput::matchAtomArg() {
    char c;
    for (int p = 0; (c = peek()) != 0;) {
        if (c == '"') {
            matchStr();
        }
        else {
            if ((c == ')' && --p < 0) || (c == ',' && p == 0)) {
                break;
            }
            p += int(c == '(');
            push(get());
            skipWs();
        }
    }
}
void AspifTextInput::matchStr() {
    matchDelim('"');
    push('"');
    auto quoted = false;
    for (char c; (c = peek()) != 0 && (c != '\"' || quoted);) {
        quoted = not quoted && c == '\\';
        push(get());
    }
    matchDelim('"');
    push('"');
}

Heuristic_t AspifTextInput::matchHeuMod() {
    auto first = peek();
    for (const auto& [k, n] : enum_entries<Heuristic_t>()) {
        if (not n.empty() && n[0] == first && ProgramReader::match(n)) {
            skipWs();
            return k;
        }
    }
    error("unrecognized heuristic modification");
    return {};
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifTextOutput
/////////////////////////////////////////////////////////////////////////////////////////
struct AspifTextOutput::Data {
    static_assert(amc::is_trivially_relocatable_v<FixedString>, "should be relocatable");
    using StringMap = std::unordered_map<FixedString, Atom_t>;
    using AtomMap   = amc::SmallVector<StringMap::const_pointer, 64>;
    using LitVec    = amc::SmallVector<Lit_t, 64>;
    using RawVec    = amc::SmallVector<uint32_t, 4096>;
    using OutVec    = amc::vector<StringMap::const_pointer>;

    static constexpr auto c_genName = StringMap::value_type{FixedString(), 0};

    [[nodiscard]] LitSpan theoryCondition(Id_t id) const {
        return {conditions.data() + id + 1, static_cast<size_t>(conditions[id])};
    }
    Id_t addTheoryCondition(const LitSpan& cond) {
        if (conditions.empty()) {
            conditions.push_back(0);
        }
        if (cond.empty()) {
            return 0;
        }
        auto id = static_cast<Id_t>(conditions.size());
        conditions.push_back(static_cast<Lit_t>(cond.size()));
        conditions.append(cond.begin(), cond.end());
        return id;
    }

    [[nodiscard]] static bool isValidAtomName(std::string_view name) {
        if (name.starts_with('-')) { // accept classical negation
            name.remove_prefix(1);
        }
        if (name.starts_with('_')) {
            name.remove_prefix(std::min(name.find_first_not_of('_', 1), name.size()));
        }
        return not name.empty() && isLower(name[0]);
    }

    void addOutput(const std::string_view& str, const LitSpan& cond) {
        out.push_back(&*strings.try_emplace(str, idMax).first);
        push(Directive_t::Output).push(static_cast<Id_t>(out.size() - 1)).push(cond);
    }

    void convertToOutput(StringMap::const_pointer node) {
        if (node->second && node->second < atoms.size()) {
            POTASSCO_CHECK_PRE(node->first[0] != '&', "Redefinition: theory atom '%u' already defined as '%s'",
                               node->second, node->first.c_str());
            atoms[node->second] = &c_genName;
            out.push_back(node);
            push(Directive_t::Output).push(static_cast<Id_t>(out.size() - 1)).push(1).push(node->second);
            const_cast<StringMap::pointer>(node)->second = 0;
        }
    }

    bool assignAtomName(Atom_t atom, const std::string_view& name) {
        POTASSCO_DEBUG_ASSERT(not name.empty());
        if (atom <= maxAtom) {
            return false;
        }
        bool theory = name[0] == '&';
        if (atom >= atoms.size()) {
            atoms.resize(atom + 1, nullptr);
        }
        if (auto* node = atoms[atom]; node) { // atom already has a tentative name
            if (node->second == atom && node->first == name) {
                return true; // identical name, ignore duplicate
            }
            convertToOutput(node); // drop assignment
            if (!theory) {
                return false;
            }
        }
        if (name.size() > 2 && name.starts_with("x_") && BufferedStream::isDigit(name[2])) {
            auto id = 0u;
            auto r  = std::from_chars(name.data() + 2, name.data() + name.size(), id);
            if (r.ec == std::errc{} && r.ptr == name.data() + name.size() && id > 0 && id <= atomMax) {
                atoms[atom] = &c_genName;
                return false;
            }
        }
        auto* node = &*strings.try_emplace(name, atom).first;
        if (node->second == atom || node->second == idMax) { // assign tentative name to atom
            node->second = atom;
            atoms[atom]  = node;
            return true;
        }
        // name already used: drop previous (tentative) assigment and prevent further assignments
        if (node->second > maxAtom) {
            convertToOutput(node);
        }
        atoms[atom] = &c_genName;
        return false;
    }
    void assignTheoryAtomName(Atom_t atom, const std::string_view& name) {
        POTASSCO_CHECK_PRE(atom > maxAtom, "Redefinition: theory atom '%u:%*s' already defined in a previous step",
                           atom, (int) name.size(), name.data());
        assignAtomName(atom, name);
    }

    [[nodiscard]] auto getAtomName(Atom_t atom) const -> const FixedString* {
        if (atom >= atoms.size() || not atoms[atom] || atoms[atom] == &c_genName) {
            return nullptr;
        }
        POTASSCO_DEBUG_ASSERT(atoms[atom]->second == atom);
        return &atoms[atom]->first;
    }

    template <typename T>
    requires(std::is_integral_v<T> || std::is_enum_v<T>)
    Data& push(T x) {
        directives.push_back(static_cast<uint32_t>(x));
        return *this;
    }
    template <std::integral T>
    requires(sizeof(T) == sizeof(uint32_t))
    Data& push(const std::span<T>& span) {
        push(span.size());
        directives.append(span.begin(), span.end());
        return *this;
    }
    Data& push(const WeightLitSpan& span) {
        directives.reserve(static_cast<RawVec::size_type>(directives.size() + (2 * span.size()) + 1));
        push(span.size());
        for (const auto& wl : span) {
            push(wl.lit);
            push(wl.weight);
        }
        return *this;
    }
    void          endStep(std::ostream&, Atom_t startAtom, bool more);
    std::ostream& printName(std::ostream& os, Lit_t lit);
    std::ostream& printName(std::ostream& os, Atom_t at) { return printName(os, lit(at)); }
    std::ostream& printCondition(std::ostream&, const uint32_t*& pos, const char* init = "");
    std::ostream& printMinimize(std::ostream&, const uint32_t*& pos);
    std::ostream& printAggregate(std::ostream&, const uint32_t*& pos, bool weights);
    template <typename T = uint32_t>
    static constexpr T next(const uint32_t*& pos) {
        return static_cast<T>(*pos++);
    }

    RawVec    directives;
    StringMap strings;
    AtomMap   atoms; // maps into strings
    OutVec    out;
    LitVec    conditions;
    Atom_t    maxAtom   = 0;
    int       showAtoms = 0;
};
AspifTextOutput::AspifTextOutput(std::ostream& os) : os_(os), data_(std::make_unique<Data>()), step_(-1) {}
AspifTextOutput::~AspifTextOutput() = default;
void AspifTextOutput::initProgram(bool incremental) {
    step_ = incremental ? 0 : -1;
    std::exchange(*data_, {});
}
void AspifTextOutput::beginStep() {
    if (step_ >= 0) {
        if (step_) {
            os_ << "% #program step(" << step_ << ").\n";
            theory_.update();
        }
        else {
            os_ << "% #program base.\n";
        }
        ++step_;
    }
}
void AspifTextOutput::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    data_->push(Directive_t::Rule).push(ht).push(head).push(Body_t::Normal).push(body);
}
void AspifTextOutput::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) {
    if (lits.empty()) {
        AspifTextOutput::rule(ht, head, {});
    }
    data_->push(Directive_t::Rule).push(static_cast<uint32_t>(ht)).push(head);
    if (std::adjacent_find(lits.begin(), lits.end(),
                           [](const auto& lhs, const auto& rhs) { return lhs.weight != rhs.weight; }) != lits.end()) {
        data_->push(Body_t::Sum).push(bound).push(lits);
    }
    else {
        bound = (bound + lits[0].weight - 1) / lits[0].weight;
        data_->push(Body_t::Count).push(bound).push(static_cast<uint32_t>(size(lits)));
        for (const auto& wl : lits) { data_->push(Potassco::lit(wl)); }
    }
}
void AspifTextOutput::minimize(Weight_t prio, const WeightLitSpan& lits) {
    data_->push(Directive_t::Minimize).push(prio).push(lits);
}
void AspifTextOutput::output(const std::string_view& str, const LitSpan& cond) {
    auto atom = cond.size() == 1 && cond.front() > 0 ? Potassco::atom(cond.front()) : 0;
    if (atom == 0 || not Data::isValidAtomName(str) || not data_->assignAtomName(atom, str)) {
        data_->addOutput(str, cond);
    }
}
void AspifTextOutput::external(Atom_t a, Value_t v) {
    data_->push(Directive_t::External).push(a).push(static_cast<uint32_t>(v));
}
void AspifTextOutput::assume(const LitSpan& lits) { data_->push(Directive_t::Assume).push(lits); }
void AspifTextOutput::project(const AtomSpan& atoms) { data_->push(Directive_t::Project).push(atoms); }
void AspifTextOutput::acycEdge(int s, int t, const LitSpan& condition) {
    data_->push(Directive_t::Edge).push(s).push(t).push(condition);
}
void AspifTextOutput::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition) {
    data_->push(Directive_t::Heuristic).push(a).push(condition).push(bias).push(prio).push(static_cast<uint32_t>(t));
}
void AspifTextOutput::theoryTerm(Id_t termId, int number) { theory_.addTerm(termId, number); }
void AspifTextOutput::theoryTerm(Id_t termId, const std::string_view& name) { theory_.addTerm(termId, name); }
void AspifTextOutput::theoryTerm(Id_t termId, int compound, const IdSpan& args) {
    if (compound >= 0)
        theory_.addTerm(termId, static_cast<Id_t>(compound), args);
    else
        theory_.addTerm(termId, Potassco::enum_cast<Tuple_t>(compound).value(), args);
}
void AspifTextOutput::theoryElement(Id_t id, const IdSpan& terms, const LitSpan& cond) {
    theory_.addElement(id, terms, data_->addTheoryCondition(cond));
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) {
    theory_.addAtom(atomOrZero, termId, elements);
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) {
    theory_.addAtom(atomOrZero, termId, elements, op, rhs);
}
void AspifTextOutput::visitTheoryAtoms() {
    for (auto it = theory_.currBegin(), end = theory_.end(); it != end; ++it) {
        if (auto atom = (*it)->atom(); not atom) {
            printTheoryAtom(os_, **it) << ".\n";
        }
        else {
            std::ostringstream str;
            printTheoryAtom(str, **it);
            data_->assignTheoryAtomName(atom, std::move(str).str());
        }
    }
}

std::ostream& AspifTextOutput::appendTerm(std::ostream& os, Id_t tId) const {
    const auto& term = theory_.getTerm(tId);
    if (term.type() == Theory_t::Number) {
        return os << term.number();
    }
    else if (term.type() == Theory_t::Symbol) {
        return os << term.symbol();
    }
    else {
        POTASSCO_CHECK_PRE(term.type() == Theory_t::Compound);
        if (term.isFunction()) {
            const auto* fSym = theory_.getTerm(term.function()).symbol();
            if (term.size() <= 2 && std::strchr("/!<=>+-*\\?&@|:;~^.", *fSym)) {
                if (auto args = term.terms(); args.size() == 2) {
                    return appendTerm(appendTerm(os, args[0]) << " "sv << fSym << " "sv, args[1]);
                }
                else {
                    return appendTerm(os << fSym, args[0]);
                }
            }
            os << fSym;
        }
        auto parens = Potassco::enum_name(term.isTuple() ? term.tuple() : Potassco::Tuple_t::Paren);
        auto sep    = ""sv;
        os << parens.substr(0, 1);
        for (auto e : term) { appendTerm(os << std::exchange(sep, ", "sv), e); }
        return os << parens.substr(1, 1);
    }
}

std::ostream& AspifTextOutput::printTheoryAtom(std::ostream& os, const TheoryAtom& atom) const {
    appendTerm(os << '&', atom.term()) << '{';
    auto sep = ""sv;
    for (auto e : atom.elements()) {
        os << std::exchange(sep, ""sv);
        const auto& elem = theory_.getElement(e);
        for (auto term : elem) { appendTerm(os << std::exchange(sep, ", "sv), term); }
        if (auto cId = elem.condition(); cId) {
            sep = " : "sv;
            for (auto lit : data_->theoryCondition(cId)) {
                os << std::exchange(sep, ", "sv);
                if (lit < 0) {
                    os << "not "sv;
                }
                data_->printName(os, Potassco::atom(lit));
            }
        }
        sep = "; "sv;
    }
    os << '}';
    if (const auto* gId = atom.guard(); gId) {
        appendTerm(os << ' ', *gId);
    }
    if (const auto* rhsId = atom.rhs(); rhsId) {
        appendTerm(os << ' ', *rhsId);
    }
    return os;
}

std::ostream& AspifTextOutput::Data::printName(std::ostream& os, Lit_t lit) {
    if (lit < 0) {
        os << "not ";
    }
    auto id = Potassco::atom(lit);
    if (const auto* name = getAtomName(id); name) {
        os << name->view();
    }
    else {
        os << "x_" << id;
        if (not showAtoms) {
            showAtoms = 1;
        }
    }
    maxAtom = std::max(maxAtom, id);
    return os;
}

std::ostream& AspifTextOutput::Data::printCondition(std::ostream& os, const uint32_t*& pos, const char* init) {
    const auto* sep     = init;
    const auto* sepNext = ", ";
    for (auto n = next(pos); n--; sep = sepNext) { printName(os << sep, next<Lit_t>(pos)); }
    return os;
}
std::ostream& AspifTextOutput::Data::printAggregate(std::ostream& os, const uint32_t*& pos, bool weights) {
    os << next<Weight_t>(pos) << " #" << (weights ? "sum" : "count") << '{';
    const auto* sep = "";
    for (auto n = next(pos), i = decltype(n)(0); n--; sep = "; ") {
        auto lit = next<Lit_t>(pos);
        os << sep;
        if (weights) {
            os << next<Weight_t>(pos) << ",";
        }
        os << ++i;
        printName(os << " : ", lit);
    }
    return os << "}";
}
std::ostream& AspifTextOutput::Data::printMinimize(std::ostream& os, const uint32_t*& pos) {
    auto prio = next<Weight_t>(pos);
    os << "#minimize{";
    const auto* sep = "";
    for (auto n = next(pos), i = decltype(n)(0); n--; sep = "; ") {
        auto lit    = next<Lit_t>(pos);
        auto weight = next<Weight_t>(pos);
        os << sep << weight << '@' << prio << ',' << ++i;
        printName(os << " : ", lit);
    }
    return os << '}';
}
void AspifTextOutput::Data::endStep(std::ostream& os, Atom_t startAtom, bool more) {
    for (const auto *pos = directives.data(), *end = pos + directives.size(); pos != end;) {
        const auto *sep = "", *term = ".";
        switch (next<Directive_t>(pos)) {
            default: POTASSCO_ASSERT_NOT_REACHED("unexpected directive");
            case Directive_t::Rule:
                term = "";
                if (next<Head_t>(pos) == Head_t::Choice) {
                    os << "{";
                    term = "}";
                }
                for (auto n = next(pos); n--; sep = !*term ? "|" : ";") { printName(os << sep, next<Atom_t>(pos)); }
                if (*sep || *term) {
                    os << term;
                    sep = " :- ";
                }
                else {
                    os << ":- ";
                }
                term = ".";
                switch (auto bt = next<Body_t>(pos)) {
                    case Body_t::Normal: printCondition(os, pos, sep); break;
                    case Body_t::Count : // fall through
                    case Body_t::Sum   : printAggregate(os << sep, pos, bt == Body_t::Sum); break;
                    default            : break;
                }
                break;
            case Directive_t::Minimize: printMinimize(os, pos); break;
            case Directive_t::Project : printCondition(os << "#project{", pos) << '}'; break;
            case Directive_t::Output:
                printCondition(os << "#show " << out.at(next(pos))->first.view(), pos, " : ");
                break;
            case Directive_t::External:
                printName(os << "#external ", next<Atom_t>(pos));
                switch (next<Value_t>(pos)) {
                    default              : break;
                    case Value_t::Free   : term = ". [free]"; break;
                    case Value_t::True   : term = ". [true]"; break;
                    case Value_t::Release: term = ". [release]"; break;
                }
                break;
            case Directive_t::Assume: printCondition(os << "#assume{", pos) << '}'; break;
            case Directive_t::Heuristic:
                term = "";
                os << "#heuristic ";
                printName(os, next<Atom_t>(pos));
                printCondition(os, pos, " : ") << ". [" << next<int32_t>(pos);
                if (auto p = next(pos)) {
                    os << "@" << p;
                }
                os << ", " << Potassco::enum_name(next<Heuristic_t>(pos)) << "]";
                break;
            case Directive_t::Edge:
                os << "#edge(" << next<int32_t>(pos) << ",";
                os << next<int32_t>(pos) << ")";
                printCondition(os, pos, " : ");
                break;
        }
        os << term << '\n';
        POTASSCO_ASSERT(pos <= end);
    }
    if (showAtoms) {
        if (showAtoms == 1) {
            os << "#show.\n";
            showAtoms = 2;
        }
        for (auto a = startAtom; a <= maxAtom; ++a) {
            if (const auto* name = getAtomName(a); name && *name->c_str() != '&') {
                os << "#show " << name->view() << " : " << name->view() << ".\n";
            }
        }
    }
    os << std::flush;
    std::exchange(directives, {});
    std::erase_if(strings, [](const auto& e) { return e.second < atomMin || e.second > atomMax; });
    if (not more) {
        std::exchange(conditions, {});
    }
}
void AspifTextOutput::endStep() {
    auto maxAtoms = data_->maxAtom;
    visitTheoryAtoms();
    data_->endStep(os_, maxAtoms + 1, step_ >= 0);
    if (step_ < 0) {
        theory_.reset();
    }
}

} // namespace Potassco
