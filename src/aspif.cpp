//
// Copyright (c) 2015 - present, Benjamin Kaufmann
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
#include <potassco/aspif.h>
#include <potassco/error.h>

#include <potassco/rule_utils.h>

#include <amc/vector.hpp>

#include <ostream>
#include <string_view>

namespace Potassco {
using namespace std::literals;
/////////////////////////////////////////////////////////////////////////////////////////
// AspifInput
/////////////////////////////////////////////////////////////////////////////////////////
struct AspifInput::Extra {
    static_assert(amc::is_trivially_relocatable_v<Id_t>, "should be relocatable");
    Atom_t                   popFact() { return facts.at(nextFact++ % facts.size()); }
    [[nodiscard]] bool       hasFact() const { return not facts.empty(); }
    RuleBuilder              rule;
    amc::vector<Id_t>        ids;
    amc::vector<Atom_t>      facts;
    amc::vector<ConstString> factTerms;
    DynamicBuffer            sym;
    uint32_t                 nextFact{0};
};

AspifInput::AspifInput(AbstractProgram& out, OutputMapping mapOutput, Atom_t fact)
    : out_(out)
    , data_(nullptr)
    , fact_(fact)
    , mapOutput_(mapOutput) {}

// asp <major> <minor> <revision>[tag...]
bool AspifInput::doAttach(bool& inc) {
    if (not match("asp "sv)) {
        return false;
    }
    version_ = matchUint(1u, 2u, "unsupported major version");
    matchUint(0u, 0u, "unsupported minor version");
    matchUint("revision number expected");
    while (match(" "sv)) {}
    inc = match("incremental"sv);
    matchChar('\n');
    out_.initProgram(inc);
    return true;
}
bool AspifInput::doParse() {
    Extra data;
    POTASSCO_SCOPE_EXIT({ data_ = nullptr; });
    data_      = &data;
    auto& rule = data.rule;
    if (fact_ && mapOutput_ == OutputMapping::atom) {
        data.facts.push_back(fact_);
    }
    out_.beginStep();
    for (AspifType rt; (rt = matchEnum<AspifType>("rule type or 0 expected")) != AspifType::end; rule.clear()) {
        switch (rt) {
            default:
                require(rt == AspifType::comment, "unrecognized rule type");
                skipLine();
                break;
            case AspifType::rule: {
                rule.start(matchEnum<HeadType>("invalid head type"));
                matchAtoms();
                if (auto bt = matchEnum<BodyType>("invalid body type"); bt == BodyType::normal) {
                    matchLits();
                    if (version_ == 1 && mapOutput_ == OutputMapping::atom && rule.isFact()) {
                        data_->facts.push_back(rule.head().front());
                    }
                }
                else {
                    require(bt == BodyType::sum, "unexpected body type");
                    rule.startSum(matchWeight());
                    matchWLits(true);
                }
                rule.end(&out_);
                break;
            }
            case AspifType::minimize:
                rule.startMinimize(matchWeight(false, "priority expected"));
                matchWLits(false);
                rule.end(&out_);
                break;
            case AspifType::project:
                matchAtoms();
                out_.project(rule.head());
                break;
            case AspifType::output: {
                if (version_ == 1) {
                    matchString();
                    matchLits();
                    if (auto cond = data_->rule.body();
                        mapOutput_ != OutputMapping::term && (cond.empty() || (cond.size() == 1 && cond.front() > 0))) {
                        if (not cond.empty()) {
                            out_.outputAtom(atom(cond.front()), data_->sym.view());
                        }
                        else if (mapOutput_ == OutputMapping::atom_fact || data_->hasFact()) {
                            auto a = mapOutput_ == OutputMapping::atom_fact ? fact_ : data.popFact();
                            out_.outputAtom(a, data_->sym.view());
                        }
                        else {
                            data.factTerms.emplace_back(data_->sym.view());
                        }
                    }
                    else {
                        outTerm(data_->sym.view(), cond);
                    }
                }
                else {
                    matchOutput(matchEnum<OutputType>("invalid output directive"));
                }
                break;
            }
            case AspifType::external:
                if (auto atom = matchAtom()) {
                    out_.external(atom, matchEnum<TruthValue>("value expected"));
                }
                break;
            case AspifType::assume:
                matchLits();
                out_.assume(rule.body());
                break;
            case AspifType::heuristic: {
                auto type = matchEnum<DomModifier>("invalid heuristic modifier");
                auto atom = matchAtom();
                auto bias = matchInt();
                auto prio = matchUint("invalid heuristic priority");
                matchLits();
                out_.heuristic(atom, type, bias, prio, rule.body());
                break;
            }
            case AspifType::edge: {
                auto start = matchInt("invalid edge, start node expected");
                auto end   = matchInt("invalid edge, end node expected");
                matchLits();
                out_.acycEdge(start, end, rule.body());
                break;
            }
            case AspifType::theory: matchTheory(matchEnum<TheoryType>("invalid theory directive")); break;
        }
    }
    for (const auto& sym : data.factTerms) {
        if (data.hasFact()) {
            out_.outputAtom(data.popFact(), sym.view());
        }
        else {
            outTerm(sym.view(), {});
        }
    }
    if (not fact_ && not data.facts.empty()) {
        fact_ = data.facts.front();
    }
    out_.endStep();
    return true;
}
void AspifInput::outTerm(std::string_view term, LitSpan cond) {
    auto tId = nextTerm_++;
    out_.outputTerm(tId, term);
    out_.output(tId, cond);
}
void AspifInput::matchAtoms() {
    for (auto len = matchUint("number of atoms expected"); len--;) { data_->rule.addHead(matchAtom()); }
}
void AspifInput::matchLits() {
    data_->rule.startBody();
    for (auto len = matchUint("number of literals expected"); len--;) { data_->rule.addGoal(matchLit()); }
}
void AspifInput::matchWLits(bool positive) {
    for (auto len = matchUint("number of literals expected"); len--;) { data_->rule.addGoal(matchWLit(positive)); }
}
void AspifInput::matchString() {
    data_->sym.clear();
    auto len = matchUint("non-negative string length expected");
    matchChar(' ');
    require(not len || stream()->read(data_->sym.alloc(len)) == len, "invalid string");
}
void AspifInput::matchIds() {
    auto len = matchUint("number of terms expected");
    data_->ids.resize(len);
    for (uint32_t i = 0; i != len; ++i) { data_->ids[i] = matchId(); }
}
void AspifInput::matchOutput(OutputType t) {
    switch (t) {
        default              : error("unrecognized output directive type");
        case OutputType::atom: { // a n s
            auto atom = matchAtom();
            matchString();
            out_.outputAtom(atom, data_->sym.view());
            break;
        }
        case OutputType::term: { // t n s
            auto term = matchId();
            matchString();
            out_.outputTerm(term, data_->sym.view());
            break;
        }
        case OutputType::cond: { // t n l1 ... ln
            auto term = matchId();
            matchLits();
            out_.output(term, data_->rule.body());
            break;
        }
    }
}
void AspifInput::matchTheory(TheoryType t) {
    auto tId = matchId();
    switch (t) {
        default                : error("unrecognized theory directive type");
        case TheoryType::number: out_.theoryTerm(tId, matchInt()); break;
        case TheoryType::symbol:
            matchString();
            out_.theoryTerm(tId, data_->sym.view());
            break;
        case TheoryType::compound: {
            auto type = matchInt("unrecognized compound term type");
            matchIds();
            out_.theoryTerm(tId, type, data_->ids);
            break;
        }
        case TheoryType::element: {
            matchIds();
            matchLits();
            out_.theoryElement(tId, data_->ids, data_->rule.body());
            break;
        }
        case TheoryType::atom: // fall through
        case TheoryType::atom_with_guard: {
            auto termId = matchId();
            matchIds();
            if (t == TheoryType::atom) {
                out_.theoryAtom(tId, termId, data_->ids);
            }
            else {
                auto opId = matchId();
                out_.theoryAtom(tId, termId, data_->ids, opId, matchId());
            }
            break;
        }
    }
}

int readAspif(std::istream& prg, AbstractProgram& out) {
    AspifInput reader(out, AspifInput::OutputMapping::atom);
    return readProgram(prg, reader);
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifOutput
/////////////////////////////////////////////////////////////////////////////////////////
constexpr auto       max_aspif_version = 2u;
static std::ostream& operator<<(std::ostream& os, WeightLit wl) { return os << lit(wl) << " " << weight(wl); }

struct AspifOutput::Data {
    void addTerm(Id_t termId, std::string_view termName) {
        if (outTerms.size() <= termId) {
            outTerms.resize(termId + 1);
        }
        POTASSCO_CHECK_PRE(outTerms[termId].name.size() == 0, "Redefinition: term %u already defined", termId);
        outTerms[termId] = termName;
    }
    struct OutTerm {
        OutTerm(std::string_view n = "") : name(n) {} // NOLINT
        ConstString name;
        Atom_t      atom{0};
        Atom_t      last{0};
    };
    amc::vector<OutTerm> outTerms;
    amc::vector<Id_t>    mapping;
    RuleBuilder          rb;
    Atom_t               trueAtom{0};
};

AspifOutput::AspifOutput(std::ostream& os, uint32_t version) : os_(os) {
    POTASSCO_CHECK_PRE(version <= max_aspif_version, "unexpected version");
    version_ = version ? version : max_aspif_version;
}
AspifOutput::~AspifOutput() = default;
auto         AspifOutput::version() const -> unsigned { return version_; }
AspifOutput& AspifOutput::startDir(AspifType r) {
    os_ << to_underlying(r);
    return *this;
}
template <typename T>
AspifOutput& AspifOutput::add(T x) {
    if constexpr (std::is_enum_v<T>) {
        os_ << " " << to_underlying(x);
    }
    else {
        os_ << " " << x;
    }
    return *this;
}
template <typename T>
AspifOutput& AspifOutput::add(std::span<T> lits) {
    os_ << " " << lits.size();
    for (const auto& l : lits) { os_ << " " << l; }
    return *this;
}
AspifOutput& AspifOutput::add(std::string_view str) {
    os_ << " " << str.size() << " ";
    os_.write(str.data(), std::ssize(str));
    return *this;
}
auto AspifOutput::map(Atom_t atom) -> Atom_t {
    if (not nextAtom_ || atom <= identityMax_) {
        identityMax_ = std::max(identityMax_, atom);
        return atom;
    }
    auto key = atom - identityMax_;
    if (key >= data_->mapping.size()) {
        data_->mapping.resize(key + 1);
    }
    auto& m = data_->mapping[key];
    if (not m) {
        m = newAtom();
    }
    return m;
}
template <typename T>
auto AspifOutput::map(std::span<T>& lits) -> std::span<T> {
    auto maxAtom = 0u;
    if (version() == 1) {
        for (auto x : lits) { maxAtom = std::max(maxAtom, atom(x)); }
    }
    if (not nextAtom_ || maxAtom <= identityMax_) {
        identityMax_ = std::max(identityMax_, maxAtom);
        return lits;
    }
    RuleBuilder& rb = data_->rb;
    if constexpr (std::is_same_v<std::remove_const_t<T>, Atom_t>) {
        rb.clearHead();
        for (auto a : lits) { rb.addHead(map(a)); }
        return rb.head();
    }
    else {
        rb.clearBody();
        if constexpr (std::is_same_v<std::remove_const_t<T>, Lit_t>) {
            for (auto x : lits) {
                auto a = map(atom(x));
                rb.addGoal(x < 0 ? neg(a) : lit(a));
            }
            return rb.body();
        }
        else {
            rb.startSum(static_cast<Weight_t>(lits.size()));
            for (auto x : lits) {
                auto a = map(atom(x));
                x.lit  = lit(x) < 0 ? neg(a) : lit(a);
                rb.addGoal(x);
            }
            return rb.sumLits();
        }
    }
}
AspifOutput& AspifOutput::endDir() {
    os_ << '\n';
    return *this;
}
void AspifOutput::initProgram(bool incremental) {
    os_ << "asp " << version() << " 0 0";
    if (incremental) {
        os_ << " incremental";
    }
    os_ << '\n';
}
void AspifOutput::auxRule(Atom_t head, LitSpan body) {
    startDir(AspifType::rule).add(HeadType::disjunctive).add(toSpan(head)).add(BodyType::normal).add(body).endDir();
}
void AspifOutput::rule(HeadType ht, AtomSpan head, LitSpan body) {
    startDir(AspifType::rule).add(ht).add(map(head)).add(BodyType::normal).add(map(body)).endDir();
}
void AspifOutput::rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) {
    startDir(AspifType::rule).add(ht).add(map(head)).add(BodyType::sum).add(bound).add(map(body)).endDir();
}
void AspifOutput::minimize(Weight_t prio, WeightLitSpan lits) {
    startDir(AspifType::minimize).add(prio).add(map(lits)).endDir();
}
void AspifOutput::outputAtom(Atom_t atom, std::string_view name) {
    POTASSCO_CHECK_PRE(atom, "atom expected");
    startDir(AspifType::output);
    if (auto a = map(atom); version() == 1) {
        auto aLit = lit(a);
        add(name).add(toSpan(aLit));
    }
    else {
        add(OutputType::atom).add(a).add(name);
    }
    endDir();
}
void AspifOutput::outputTerm(Id_t termId, std::string_view name) {
    if (version() != 1) {
        startDir(AspifType::output).add(OutputType::term).add(termId).add(name).endDir();
    }
    else {
        if (not data_) {
            data_ = std::make_unique<Data>();
        }
        data_->addTerm(termId, name);
    }
}
void AspifOutput::output(Id_t id, LitSpan cond) {
    cond = map(cond);
    if (version() != 1) {
        startDir(AspifType::output).add(OutputType::cond).add(id).add(cond).endDir();
    }
    else {
        POTASSCO_CHECK_PRE(data_ && id < data_->outTerms.size(), "Undefined: term %u is unknown", id);
        if (not data_->trueAtom) {
            nextAtom_       = identityMax_ + 1;
            data_->trueAtom = newAtom();
            auxRule(data_->trueAtom, LitSpan{});
        }
        auto& term = data_->outTerms[id];
        if (not term.atom) {
            // Ensure that the output condition has two elements so that it is not transformed back to an atom.
            term.atom         = newAtom();
            Lit_t termBody[2] = {lit(term.atom), lit(data_->trueAtom)};
            startDir(AspifType::output).add(term.name.view()).add(LitSpan{termBody}).endDir();
        }
        Lit_t    termBody[2];
        uint32_t bs  = 0;
        Lit_t    aux = 0;
        if (not cond.empty()) {
            termBody[bs++] = cond.size() > 1 ? (aux = lit(newAtom())) : cond.front();
        }
        if (term.last) {
            termBody[bs++] = neg(term.last);
        }
        auxRule(term.atom, LitSpan{termBody, bs}); // termAtom :- aux, [not term.last]
        if (aux) {
            auxRule(atom(aux), cond);
        }
    }
}
void AspifOutput::external(Atom_t a, TruthValue v) { startDir(AspifType::external).add(map(a)).add(v).endDir(); }
void AspifOutput::assume(LitSpan lits) { startDir(AspifType::assume).add(map(lits)).endDir(); }
void AspifOutput::project(AtomSpan atoms) { startDir(AspifType::project).add(map(atoms)).endDir(); }
void AspifOutput::acycEdge(int s, int t, LitSpan condition) {
    startDir(AspifType::edge).add(s).add(t).add(map(condition)).endDir();
}
void AspifOutput::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) {
    startDir(AspifType::heuristic).add(t).add(map(a)).add(bias).add(prio).add(map(condition)).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int number) {
    startDir(AspifType::theory).add(TheoryType::number).add(termId).add(number).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, std::string_view name) {
    startDir(AspifType::theory).add(TheoryType::symbol).add(termId).add(name).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int compound, IdSpan args) {
    startDir(AspifType::theory).add(TheoryType::compound).add(termId).add(compound).add(args).endDir();
}
void AspifOutput::theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) {
    startDir(AspifType::theory).add(TheoryType::element).add(elementId).add(terms).add(map(cond)).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) {
    startDir(AspifType::theory).add(TheoryType::atom).add(map(atomOrZero)).add(termId).add(elements).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) {
    startDir(AspifType::theory)
        .add(TheoryType::atom_with_guard)
        .add(map(atomOrZero))
        .add(termId)
        .add(elements)
        .add(op)
        .add(rhs)
        .endDir();
}

void AspifOutput::beginStep() {
    if (data_) {
        for (auto& t : data_->outTerms) {
            if (auto prev = std::exchange(t.atom, 0u); prev) {
                t.last = prev;
            }
        }
    }
}
void AspifOutput::endStep() { os_ << "0\n"; }
} // namespace Potassco
