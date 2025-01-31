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
#include <potassco/theory_data.h>

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
    RuleBuilder       rule;
    amc::vector<Id_t> ids;
    DynamicBuffer     sym;
};

AspifInput::AspifInput(AbstractProgram& out) : out_(out), data_(nullptr) {}

// asp <major> <minor> <revision>[tag...]
bool AspifInput::doAttach(bool& inc) {
    if (not match("asp "sv)) {
        return false;
    }
    matchUint(1u, 1u, "unsupported major version");
    matchUint(0u, 0u, "unsupported minor version");
    matchUint("revision number expected");
    while (match(" "sv)) { ; }
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
                matchString();
                matchLits();
                out_.output(data.sym.view(), rule.body());
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
    out_.endStep();
    return true;
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
void AspifInput::matchTheory(TheoryType t) {
    auto tId = matchId();
    switch (t) {
        default                : error("unrecognized theory directive type"); return;
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
    AspifInput reader(out);
    return readProgram(prg, reader);
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifOutput
/////////////////////////////////////////////////////////////////////////////////////////
static std::ostream& operator<<(std::ostream& os, WeightLit wl) { return os << lit(wl) << " " << weight(wl); }

AspifOutput::AspifOutput(std::ostream& os) : os_(os) {}

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
AspifOutput& AspifOutput::add(std::span<const T> lits) {
    os_ << " " << lits.size();
    for (const auto& l : lits) { os_ << " " << l; }
    return *this;
}
AspifOutput& AspifOutput::add(std::string_view str) {
    os_ << " " << str.size() << " ";
    os_.write(str.data(), std::ssize(str));
    return *this;
}
AspifOutput& AspifOutput::endDir() {
    os_ << '\n';
    return *this;
}
void AspifOutput::initProgram(bool inc) {
    os_ << "asp 1 0 0";
    if (inc) {
        os_ << " incremental";
    }
    os_ << '\n';
}
void AspifOutput::rule(HeadType ht, AtomSpan head, LitSpan body) {
    startDir(AspifType::rule).add(ht).add(head).add(BodyType::normal).add(body).endDir();
}
void AspifOutput::rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) {
    startDir(AspifType::rule).add(ht).add(head).add(BodyType::sum).add(bound).add(body).endDir();
}
void AspifOutput::minimize(Weight_t prio, WeightLitSpan lits) {
    startDir(AspifType::minimize).add(prio).add(lits).endDir();
}
void AspifOutput::output(std::string_view str, LitSpan cond) {
    startDir(AspifType::output).add(str).add(cond).endDir();
}
void AspifOutput::external(Atom_t a, TruthValue v) { startDir(AspifType::external).add(a).add(v).endDir(); }
void AspifOutput::assume(LitSpan lits) { startDir(AspifType::assume).add(lits).endDir(); }
void AspifOutput::project(AtomSpan atoms) { startDir(AspifType::project).add(atoms).endDir(); }
void AspifOutput::acycEdge(int s, int t, LitSpan cond) { startDir(AspifType::edge).add(s).add(t).add(cond).endDir(); }
void AspifOutput::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan cond) {
    startDir(AspifType::heuristic).add(t).add(a).add(bias).add(prio).add(cond).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int number) {
    startDir(AspifType::theory).add(TheoryType::number).add(termId).add(number).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, std::string_view name) {
    startDir(AspifType::theory).add(TheoryType::symbol).add(termId).add(name).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int cId, IdSpan args) {
    startDir(AspifType::theory).add(TheoryType::compound).add(termId).add(cId).add(args).endDir();
}
void AspifOutput::theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) {
    startDir(AspifType::theory).add(TheoryType::element).add(elementId).add(terms).add(cond).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) {
    startDir(AspifType::theory).add(TheoryType::atom).add(atomOrZero).add(termId).add(elements).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) {
    startDir(AspifType::theory)
        .add(TheoryType::atom_with_guard)
        .add(atomOrZero)
        .add(termId)
        .add(elements)
        .add(op)
        .add(rhs)
        .endDir();
}

void AspifOutput::beginStep() {}
void AspifOutput::endStep() { os_ << "0\n"; }
} // namespace Potassco
