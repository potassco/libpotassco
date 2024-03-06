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

#include <potassco/rule_utils.h>
#include <potassco/string_convert.h>

#include <cctype>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>
namespace Potassco {
struct AspifTextInput::Data {
    void clear() {
        rule.clear();
        symbol.clear();
    }
    [[nodiscard]] AtomSpan atoms() const { return rule.head(); }
    [[nodiscard]] LitSpan  lits() const { return rule.body(); }

    RuleBuilder rule;
    std::string symbol;
};
AspifTextInput::AspifTextInput(AbstractProgram* out) : out_(out), data_(nullptr) {}
void AspifTextInput::setOutput(AbstractProgram& out) { out_ = &out; }
bool AspifTextInput::doAttach(bool& inc) {
    auto n = peek(true);
    if (out_ && (!n || std::islower(static_cast<unsigned char>(n)) || std::strchr(".#%{:", n))) {
        while (n == '%') {
            skipLine();
            n = peek(true);
        }
        inc = matchOpt("#incremental");
        if (inc)
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
    POTASSCO_SCOPE_EXIT({ data_ = nullptr; });
    Data data;
    data_ = &data;
    for (char c; (c = peek(true)) != 0; data.clear()) {
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
        matchAtoms(";,");
        matchDelim('}');
    }
    else {
        data_->rule.start();
        matchAtoms(";|");
    }
    if (matchOpt(":-")) {
        c = peek(true);
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
    if (matchOpt("#minimize")) {
        data_->rule.startMinimize(0);
        matchAgg();
        Weight_t prio = matchOpt("@") ? matchInt() : 0;
        matchDelim('.');
        data_->rule.setBound(prio);
        data_->rule.end(out_);
    }
    else if (matchOpt("#project")) {
        data_->rule.start();
        if (matchOpt("{")) {
            matchAtoms(",");
            matchDelim('}');
        }
        matchDelim('.');
        out_->project(data_->atoms());
    }
    else if (matchOpt("#output")) {
        matchTerm();
        matchCondition();
        matchDelim('.');
        out_->output(data_->symbol, data_->lits());
    }
    else if (matchOpt("#external")) {
        Atom_t  a = matchId();
        Value_t v = Value_t::False;
        matchDelim('.');
        if (matchOpt("[")) {
            if (matchOpt("true")) {
                v = Value_t::True;
            }
            else if (matchOpt("free")) {
                v = Value_t::Free;
            }
            else if (matchOpt("release")) {
                v = Value_t::Release;
            }
            else {
                require(matchOpt("false"), "<value> expected");
            }
            matchDelim(']');
        }
        out_->external(a, v);
    }
    else if (matchOpt("#assume")) {
        data_->rule.startBody();
        if (matchOpt("{")) {
            matchLits();
            matchDelim('}');
        }
        matchDelim('.');
        out_->assume(data_->lits());
    }
    else if (matchOpt("#heuristic")) {
        Atom_t a = matchId();
        matchCondition();
        matchDelim('.');
        matchDelim('[');
        int v = matchInt();
        int p = 0;
        if (matchOpt("@")) {
            p = matchInt();
            require(p >= 0, "positive priority expected");
        }
        matchDelim(',');
        const char* w = matchWord();
        Heuristic_t ht;
        require(Potassco::match(w, ht), "unrecognized heuristic modification");
        skipws();
        matchDelim(']');
        out_->heuristic(a, ht, v, static_cast<unsigned>(p), data_->lits());
    }
    else if (matchOpt("#edge")) {
        int s, t;
        matchDelim('('), s = matchInt(), matchDelim(','), t = matchInt(), matchDelim(')');
        matchCondition();
        matchDelim('.');
        out_->acycEdge(s, t, data_->lits());
    }
    else if (matchOpt("#step")) {
        require(incremental(), "#step requires incremental program");
        matchDelim('.');
        return false;
    }
    else if (matchOpt("#incremental")) {
        matchDelim('.');
    }
    else {
        require(false, "unrecognized directive");
    }
    return true;
}

void AspifTextInput::skipws() { stream()->skipWs(); }
bool AspifTextInput::matchOpt(const char* term) {
    if (ProgramReader::match(term, false)) {
        skipws();
        return true;
    }
    return false;
}

void AspifTextInput::matchDelim(char c) {
    if (stream()->get() == c)
        return skipws();

    char err[] = "'x' expected";
    err[1]     = c;
    require(false, err);
}

void AspifTextInput::matchAtoms(const char* seps) {
    if (std::islower(static_cast<unsigned char>(peek(true))) != 0) {
        do {
            Lit_t x = matchLit();
            require(x > 0, "positive atom expected");
            data_->rule.addHead(static_cast<Atom_t>(x));
        } while (std::strchr(seps, stream()->peek()) && stream()->get() && (skipws(), true));
    }
}
void AspifTextInput::matchLits() {
    if (std::islower(static_cast<unsigned char>(peek(true))) != 0) {
        do { data_->rule.addGoal(matchLit()); } while (matchOpt(","));
    }
}
void AspifTextInput::matchCondition() {
    data_->rule.startBody();
    if (matchOpt(":")) {
        matchLits();
    }
}
void AspifTextInput::matchAgg() {
    matchDelim('{');
    if (not matchOpt("}")) {
        do {
            WeightLit_t wl = {matchLit(), 1};
            if (matchOpt("=")) {
                wl.weight = matchInt();
            }
            data_->rule.addGoal(wl);
        } while (matchOpt(","));
        matchDelim('}');
    }
}

Lit_t AspifTextInput::matchLit() {
    int s = matchOpt("not ") ? -1 : 1;
    return static_cast<Lit_t>(matchId()) * s;
}

int AspifTextInput::matchInt() {
    int i = ProgramReader::matchInt();
    skipws();
    return i;
}
Atom_t AspifTextInput::matchId() {
    char c = stream()->get();
    char n = stream()->peek();
    require(std::islower(static_cast<unsigned char>(c)) != 0, "<id> expected");
    require(std::islower(static_cast<unsigned char>(n)) == 0, "<pos-integer> expected");
    if (c == 'x' && (BufferedStream::isDigit(n) || n == '_')) {
        if (n == '_') {
            stream()->get();
        }
        int i = matchInt();
        require(i > 0, "<pos-integer> expected");
        return static_cast<Atom_t>(i);
    }
    else {
        skipws();
        return static_cast<Atom_t>(c - 'a') + 1;
    }
}
void AspifTextInput::push(char c) { data_->symbol.append(1, c); }

void AspifTextInput::matchTerm() {
    char c = stream()->peek();
    if (std::islower(static_cast<unsigned char>(c)) != 0 || c == '_') {
        do {
            push(stream()->get());
        } while (std::isalnum(static_cast<unsigned char>(c = stream()->peek())) != 0 || c == '_');
        skipws();
        if (matchOpt("(")) {
            push('(');
            for (;;) {
                matchAtomArg();
                if (not matchOpt(","))
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
        require(false, "<term> expected");
    }
    skipws();
}
void AspifTextInput::matchAtomArg() {
    char c;
    for (int p = 0; (c = stream()->peek()) != 0;) {
        if (c == '"') {
            matchStr();
        }
        else {
            if ((c == ')' && --p < 0) || (c == ',' && p == 0)) {
                break;
            }
            p += int(c == '(');
            push(stream()->get());
            skipws();
        }
    }
}
void AspifTextInput::matchStr() {
    matchDelim('"');
    push('"');
    bool quoted = false;
    for (char c; (c = stream()->peek()) != 0 && (c != '\"' || quoted);) {
        quoted = not quoted && c == '\\';
        push(stream()->get());
    }
    matchDelim('"');
    push('"');
}

const char* AspifTextInput::matchWord() {
    data_->symbol.clear();
    for (char c; (c = stream()->peek()) != 0 && std::isalnum(static_cast<unsigned char>(c));) { push(stream()->get()); }
    return data_->symbol.c_str();
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifTextOutput
/////////////////////////////////////////////////////////////////////////////////////////
struct AspifTextOutput::Data {
    using StringVec = std::vector<std::string>;
    using AtomMap   = std::vector<Id_t>;
    using LitVec    = std::vector<Lit_t>;
    using RawVec    = std::vector<uint32_t>;
    [[nodiscard]] LitSpan getCondition(Id_t id) const {
        return {&conditions[id + 1], static_cast<size_t>(conditions[id])};
    }
    Id_t addCondition(const LitSpan& cond) {
        if (conditions.empty()) {
            conditions.push_back(0);
        }
        if (empty(cond)) {
            return 0;
        }
        Id_t id = static_cast<Id_t>(conditions.size());
        conditions.push_back(static_cast<Lit_t>(size(cond)));
        conditions.insert(conditions.end(), begin(cond), end(cond));
        return id;
    }
    Id_t addString(const std::string_view& str) {
        Id_t id = static_cast<Id_t>(strings.size());
        strings.emplace_back(str);
        return id;
    }
    RawVec    directives;
    StringVec strings;
    AtomMap   atoms; // maps into strings
    LitVec    conditions;
    uint32_t  readPos{};
    void      reset() {
        directives.clear();
        strings.clear();
        atoms.clear();
        conditions.clear();
        readPos = 0;
    }
};
AspifTextOutput::AspifTextOutput(std::ostream& os) : os_(os), step_(-1) { data_ = new Data(); }
AspifTextOutput::~AspifTextOutput() { delete data_; }
void AspifTextOutput::addAtom(Atom_t id, const std::string_view& str) {
    if (id >= data_->atoms.size()) {
        data_->atoms.resize(id + 1, idMax);
    }
    data_->atoms[id] = data_->addString(str);
}
std::ostream& AspifTextOutput::printName(std::ostream& os, Lit_t lit) const {
    if (lit < 0) {
        os << "not ";
    }
    Atom_t id = Potassco::atom(lit);
    if (id < data_->atoms.size() && data_->atoms[id] < data_->strings.size()) {
        os << data_->strings[data_->atoms[id]];
    }
    else {
        os << "x_" << id;
    }
    return os;
}
void AspifTextOutput::initProgram(bool incremental) {
    step_ = incremental ? 0 : -1;
    data_->reset();
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
    push(Directive_t::Rule).push(ht).push(head).push(Body_t::Normal).push(body);
}
void AspifTextOutput::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) {
    if (size(lits) == 0) {
        AspifTextOutput::rule(ht, head, {});
    }
    push(Directive_t::Rule).push(static_cast<uint32_t>(ht)).push(head);
    auto top = static_cast<uint32_t>(data_->directives.size());
    auto min = weight(*begin(lits)), max = min;
    push(Body_t::Sum).push(bound).push(static_cast<uint32_t>(size(lits)));
    for (const auto& wl : lits) {
        push(Potassco::lit(wl)).push(Potassco::weight(wl));
        if (Potassco::weight(wl) < min) {
            min = Potassco::weight(wl);
        }
        if (Potassco::weight(wl) > max) {
            max = Potassco::weight(wl);
        }
    }
    if (min == max) {
        data_->directives.resize(top);
        bound = (bound + min - 1) / min;
        push(Body_t::Count).push(bound).push(static_cast<uint32_t>(size(lits)));
        for (const auto& wl : lits) { push(Potassco::lit(wl)); }
    }
}
void AspifTextOutput::minimize(Weight_t prio, const WeightLitSpan& lits) {
    push(Directive_t::Minimize).push(lits).push(prio);
}
void AspifTextOutput::output(const std::string_view& str, const LitSpan& cond) {
    bool isAtom = size(str) > 0 && (std::islower(static_cast<unsigned char>(*begin(str))) || *begin(str) == '_');
    if (size(cond) == 1 && lit(*begin(cond)) > 0 && isAtom) {
        addAtom(Potassco::atom(*begin(cond)), str);
    }
    else {
        push(Directive_t::Output).push(data_->addString(str)).push(cond);
    }
}
void AspifTextOutput::external(Atom_t a, Value_t v) {
    push(Directive_t::External).push(a).push(static_cast<uint32_t>(v));
}
void AspifTextOutput::assume(const LitSpan& lits) { push(Directive_t::Assume).push(lits); }
void AspifTextOutput::project(const AtomSpan& atoms) { push(Directive_t::Project).push(atoms); }
void AspifTextOutput::acycEdge(int s, int t, const LitSpan& condition) {
    push(Directive_t::Edge).push(s).push(t).push(condition);
}
void AspifTextOutput::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition) {
    push(Directive_t::Heuristic).push(a).push(condition).push(bias).push(prio).push(static_cast<uint32_t>(t));
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
    theory_.addElement(id, terms, data_->addCondition(cond));
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) {
    theory_.addAtom(atomOrZero, termId, elements);
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) {
    theory_.addAtom(atomOrZero, termId, elements, op, rhs);
}
template <class T>
T AspifTextOutput::get() {
    return static_cast<T>(data_->directives[data_->readPos++]);
}
AspifTextOutput& AspifTextOutput::push(uint32_t x) {
    data_->directives.push_back(x);
    return *this;
}
AspifTextOutput& AspifTextOutput::push(const AtomSpan& atoms) {
    data_->directives.push_back(static_cast<uint32_t>(size(atoms)));
    data_->directives.insert(data_->directives.end(), begin(atoms), end(atoms));
    return *this;
}
AspifTextOutput& AspifTextOutput::push(const LitSpan& lits) {
    data_->directives.push_back(static_cast<uint32_t>(size(lits)));
    data_->directives.insert(data_->directives.end(), begin(lits), end(lits));
    return *this;
}
AspifTextOutput& AspifTextOutput::push(const WeightLitSpan& wlits) {
    data_->directives.reserve(data_->directives.size() + (2 * size(wlits)));
    data_->directives.push_back(static_cast<uint32_t>(size(wlits)));
    for (const auto& wl : wlits) {
        data_->directives.push_back(static_cast<uint32_t>(lit(wl)));
        data_->directives.push_back(static_cast<uint32_t>(weight(wl)));
    }
    return *this;
}
void AspifTextOutput::writeDirectives() {
    data_->readPos = 0;
    for (Directive_t x; (x = get<Directive_t>()) != Directive_t::End;) {
        auto sep = "", term = "";
        switch (x) {
            case Directive_t::Rule:
                if (get<uint32_t>() != 0) {
                    os_ << "{";
                    term = "}";
                }
                for (auto n = get<uint32_t>(); n--; sep = !*term ? "|" : ";") { printName(os_ << sep, get<Atom_t>()); }
                if (*sep) {
                    os_ << term;
                    sep = " :- ";
                }
                else {
                    os_ << ":- ";
                }
                term = ".";
                switch (auto bt = get<Body_t>()) {
                    case Body_t::Normal:
                        for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                        break;
                    case Body_t::Count: // fall through
                    case Body_t::Sum:
                        os_ << sep << get<Weight_t>();
                        sep = "{";
                        for (auto n = get<uint32_t>(); n--; sep = "; ") {
                            printName(os_ << sep, get<Lit_t>());
                            if (bt == Body_t::Sum) {
                                os_ << "=" << get<Weight_t>();
                            }
                        }
                        os_ << "}";
                        break;
                    default: break;
                }
                break;
            case Directive_t::Minimize:
                sep  = "#minimize{";
                term = ".";
                for (auto n = get<uint32_t>(); n--; sep = "; ") {
                    printName(os_ << sep, get<Lit_t>());
                    os_ << "=" << get<Weight_t>();
                }
                os_ << "}@" << get<Weight_t>();
                break;
            case Directive_t::Project:
                sep  = "#project{";
                term = "}.";
                for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                break;
            case Directive_t::Output:
                sep  = " : ";
                term = ".";
                os_ << "#show " << data_->strings[get<uint32_t>()];
                for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                break;
            case Directive_t::External:
                sep  = "#external ";
                term = ".";
                printName(os_ << sep, get<Atom_t>());
                switch (get<Value_t>()) {
                    default              : break;
                    case Value_t::Free   : term = ". [free]"; break;
                    case Value_t::True   : term = ". [true]"; break;
                    case Value_t::Release: term = ". [release]"; break;
                }
                break;
            case Directive_t::Assume:
                sep  = "#assume{";
                term = "}.";
                for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                break;
            case Directive_t::Heuristic:
                sep  = " : ";
                term = "";
                os_ << "#heuristic ";
                printName(os_, get<Atom_t>());
                for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                os_ << ". [" << get<int32_t>();
                if (auto p = get<uint32_t>()) {
                    os_ << "@" << p;
                }
                os_ << ", " << toString(get<Heuristic_t>()) << "]";
                break;
            case Directive_t::Edge:
                sep  = " : ";
                term = ".";
                os_ << "#edge(" << get<int32_t>() << ",";
                os_ << get<int32_t>() << ")";
                for (auto n = get<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, get<Lit_t>()); }
                break;
            default: POTASSCO_ASSERT(false, "unexpected directive");
        }
        os_ << term << "\n";
    }
}
void AspifTextOutput::visitTheories() {
    struct BuildStr : public TheoryAtomStringBuilder {
        explicit BuildStr(AspifTextOutput& s) : self(&s) {}
        [[nodiscard]] LitSpan     getCondition(Id_t condId) const override { return self->data_->getCondition(condId); }
        [[nodiscard]] std::string getName(Atom_t id) const override {
            if (id < self->data_->atoms.size() && self->data_->atoms[id] < self->data_->strings.size()) {
                return self->data_->strings[self->data_->atoms[id]];
            }
            return std::string("x_").append(Potassco::toString(id));
        }
        AspifTextOutput* self;
    } toStr(*this);
    for (auto it = theory_.currBegin(), end = theory_.end(); it != end; ++it) {
        Atom_t      atom = (*it)->atom();
        std::string name = toStr.toString(theory_, **it);
        if (not atom) {
            os_ << name << ".\n";
        }
        else {
            POTASSCO_REQUIRE(atom >= data_->atoms.size() || data_->atoms[atom] == idMax,
                             "Redefinition: theory atom '%u' already shown as '%s'", atom,
                             data_->strings[data_->atoms[atom]].c_str());
            addAtom(atom, name);
        }
    }
}
void AspifTextOutput::endStep() {
    visitTheories();
    push(Directive_t::End);
    writeDirectives();
    Data::RawVec().swap(data_->directives);
    if (step_ < 0) {
        theory_.reset();
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
// TheoryAtomStringBuilder
/////////////////////////////////////////////////////////////////////////////////////////
std::string TheoryAtomStringBuilder::toString(const TheoryData& td, const TheoryAtom& a) {
    res_.clear();
    add('&').term(td, td.getTerm(a.term())).add('{');
    const char* sep = "";
    for (const auto& e : a) { add(std::exchange(sep, "; ")).element(td, td.getElement(e)); }
    add('}');
    if (a.guard()) {
        add(' ').term(td, td.getTerm(*a.guard()));
    }
    if (a.rhs()) {
        add(' ').term(td, td.getTerm(*a.rhs()));
    }
    return res_;
}
bool TheoryAtomStringBuilder::function(const TheoryData& td, const TheoryTerm& f) {
    TheoryTerm x = td.getTerm(f.function());
    if (x.type() == Theory_t::Symbol && std::strchr("/!<=>+-*\\?&@|:;~^.", *x.symbol()) != nullptr) {
        if (f.size() == 1) {
            term(td, x).term(td, td.getTerm(*f.begin()));
            return false;
        }
        else if (f.size() == 2) {
            term(td, td.getTerm(*f.begin())).add(' ').term(td, x).add(' ').term(td, td.getTerm(*(f.begin() + 1)));
            return false;
        }
    }
    term(td, x);
    return true;
}
TheoryAtomStringBuilder& TheoryAtomStringBuilder::term(const TheoryData& data, const TheoryTerm& t) {
    switch (t.type()) {
        default                : POTASSCO_ASSERT(false, "unrecognized term");
        case Theory_t::Number  : add(Potassco::toString(t.number())); break;
        case Theory_t::Symbol  : add(t.symbol()); break;
        case Theory_t::Compound: {
            if (not t.isFunction() || function(data, t)) {
                auto        parens = Potassco::enum_name(t.isTuple() ? t.tuple() : Potassco::Tuple_t::Paren);
                const char* sep    = "";
                add(parens.at(0));
                for (const auto& e : t) { add(std::exchange(sep, ", ")).term(data, data.getTerm(e)); }
                add(parens.at(1));
            }
        }
    }
    return *this;
}
TheoryAtomStringBuilder& TheoryAtomStringBuilder::element(const TheoryData& data, const TheoryElement& e) {
    const char* sep = "";
    for (const auto& t : e) { add(std::exchange(sep, ", ")).term(data, data.getTerm(t)); }
    if (e.condition()) {
        LitSpan cond = getCondition(e.condition());
        sep          = " : ";
        for (const auto& lit : cond) {
            add(std::exchange(sep, ", "));
            if (lit < 0) {
                add("not ");
            }
            add(getName(atom(lit)));
        }
    }
    return *this;
}
} // namespace Potassco
