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

#include <potassco/rule_utils.h>
#include <potassco/theory_data.h>

#include <amc/vector.hpp>

#include <ostream>
#include <string_view>

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// AspifInput
/////////////////////////////////////////////////////////////////////////////////////////
struct AspifInput::Extra {
    static_assert(amc::is_trivially_relocatable_v<Id_t>, "should be relocatable");
    amc::SmallVector<Id_t, 64>  ids;
    amc::SmallVector<char, 256> sym;

    [[nodiscard]] std::string_view symView() const noexcept { return {sym.data(), sym.size()}; }
};

AspifInput::AspifInput(AbstractProgram& out) : out_(out), rule_(nullptr), data_(nullptr) {}

bool AspifInput::doAttach(bool& inc) {
    if (not match("asp ")) {
        return false;
    }
    require(matchPos() == 1, "unsupported major version");
    require(matchPos() == 0, "unsupported minor version");
    matchPos("revision number expected");
    while (match(" ", false)) { ; }
    inc = match("incremental", false);
    out_.initProgram(inc);
    return require(stream()->get() == '\n', "invalid extra characters in problem line");
}
bool AspifInput::doParse() {
    RuleBuilder rule;
    Extra       data;
    rule_ = &rule;
    data_ = &data;
    out_.beginStep();
    for (Directive_t rt; (rt = matchType<Directive_t>("rule type or 0 expected")) != Directive_t::End; rule.clear()) {
        switch (rt) {
            default:
                require(rt == Directive_t::Comment, "unrecognized rule type");
                skipLine();
                break;
            case Directive_t::Rule: {
                rule.start(matchType<Head_t>("invalid head type"));
                matchAtoms();
                auto bt = matchType<Body_t>("invalid body type");
                if (bt == Body_t::Normal) {
                    matchLits();
                }
                else {
                    rule.startSum(matchInt());
                    matchWLits(0);
                }
                rule.end(&out_);
                break;
            }
            case Directive_t::Minimize:
                rule.startMinimize(matchInt());
                matchWLits(INT_MIN);
                rule.end(&out_);
                break;
            case Directive_t::Project:
                matchAtoms();
                out_.project(rule.head());
                break;
            case Directive_t::Output: {
                matchString();
                matchLits();
                out_.output(data.symView(), rule.body());
                break;
            }
            case Directive_t::External:
                if (Atom_t atom = matchAtom()) {
                    auto val = matchType<Value_t>("value expected");
                    out_.external(atom, val);
                }
                break;
            case Directive_t::Assume:
                matchLits();
                out_.assume(rule.body());
                break;
            case Directive_t::Heuristic: {
                auto type = matchType<Heuristic_t>("invalid heuristic modifier");
                auto atom = matchAtom();
                auto bias = matchInt();
                auto prio = matchPos(INT_MAX, "invalid heuristic priority");
                matchLits();
                out_.heuristic(atom, type, bias, prio, rule.body());
                break;
            }
            case Directive_t::Edge: {
                auto start = matchPos(INT_MAX, "invalid edge, start node expected");
                auto end   = matchPos(INT_MAX, "invalid edge, end node expected");
                matchLits();
                out_.acycEdge((int) start, (int) end, rule.body());
                break;
            }
            case Directive_t::Theory: matchTheory(static_cast<Theory_t>(matchPos())); break;
        }
    }
    out_.endStep();
    rule_ = nullptr;
    data_ = nullptr;
    return true;
}

void AspifInput::matchAtoms() {
    for (auto len = matchPos("number of atoms expected"); len--;) { rule_->addHead(matchAtom()); }
}
void AspifInput::matchLits() {
    rule_->startBody();
    for (auto len = matchPos("number of literals expected"); len--;) { rule_->addGoal(matchLit()); }
}
void AspifInput::matchWLits(int32_t minW) {
    for (auto len = matchPos("number of literals expected"); len--;) { rule_->addGoal(matchWLit(minW)); }
}
void AspifInput::matchString() {
    auto len = matchPos("non-negative string length expected");
    stream()->get();
    data_->sym.resize(len);
    require(not len || stream()->copy(data_->sym) == len, "invalid string");
}
void AspifInput::matchIds() {
    auto len = matchPos("number of terms expected");
    data_->ids.resize(len);
    for (uint32_t i = 0; i != len; ++i) { data_->ids[i] = matchPos(); }
}
void AspifInput::matchTheory(Theory_t rt) {
    auto tId = matchPos();
    switch (rt) {
        default              : error("unrecognized theory directive type"); return;
        case Theory_t::Number: out_.theoryTerm(tId, matchInt()); break;
        case Theory_t::Symbol:
            matchString();
            out_.theoryTerm(tId, data_->symView());
            break;
        case Theory_t::Compound: {
            int type = matchInt(-static_cast<int>(enum_count<Tuple_t>()), INT_MAX, "unrecognized compound term type");
            matchIds();
            out_.theoryTerm(tId, type, data_->ids);
            break;
        }
        case Theory_t::Element: {
            matchIds();
            matchLits();
            out_.theoryElement(tId, data_->ids, rule_->body());
            break;
        }
        case Theory_t::Atom: // fall through
        case Theory_t::AtomWithGuard: {
            auto termId = matchPos();
            matchIds();
            if (static_cast<Theory_t>(rt) == Theory_t::Atom) {
                out_.theoryAtom(tId, termId, data_->ids);
            }
            else {
                auto opId = matchPos();
                out_.theoryAtom(tId, termId, data_->ids, opId, matchPos());
            }
            break;
        }
    }
}

int readAspif(std::istream& in, AbstractProgram& out) {
    AspifInput reader(out);
    return readProgram(in, reader);
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifOutput
/////////////////////////////////////////////////////////////////////////////////////////
AspifOutput::AspifOutput(std::ostream& os) : os_(os) {}

AspifOutput& AspifOutput::startDir(Directive_t r) {
    os_ << static_cast<unsigned>(r);
    return *this;
}
AspifOutput& AspifOutput::add(int x) {
    os_ << " " << x;
    return *this;
}
AspifOutput& AspifOutput::add(unsigned x) {
    os_ << " " << x;
    return *this;
}
AspifOutput& AspifOutput::add(const WeightLitSpan& lits) {
    os_ << " " << lits.size();
    for (const auto& wl : lits) { os_ << " " << lit(wl) << " " << weight(wl); }
    return *this;
}
AspifOutput& AspifOutput::add(const LitSpan& lits) {
    os_ << " " << lits.size();
    for (const auto& x : lits) { os_ << " " << lit(x); }
    return *this;
}
AspifOutput& AspifOutput::add(const AtomSpan& atoms) {
    os_ << " " << atoms.size();
    for (const auto& a : atoms) { os_ << " " << a; }
    return *this;
}
AspifOutput& AspifOutput::add(const std::string_view& str) {
    os_ << " " << str.size() << " ";
    os_.write(str.data(), std::ssize(str));
    return *this;
}
AspifOutput& AspifOutput::endDir() {
    os_ << "\n";
    return *this;
}
void AspifOutput::initProgram(bool inc) {
    os_ << "asp 1 0 0";
    if (inc)
        os_ << " incremental";
    os_ << "\n";
}
void AspifOutput::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    startDir(Directive_t::Rule)
        .add(static_cast<int>(ht))
        .add(head)
        .add(static_cast<int>(Body_t::Normal))
        .add(body)
        .endDir();
}
void AspifOutput::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    startDir(Directive_t::Rule)
        .add(static_cast<int>(ht))
        .add(head)
        .add(static_cast<int>(Body_t::Sum))
        .add(static_cast<int>(bound))
        .add(body)
        .endDir();
}
void AspifOutput::minimize(Weight_t prio, const WeightLitSpan& lits) {
    startDir(Directive_t::Minimize).add(prio).add(lits).endDir();
}
void AspifOutput::output(const std::string_view& str, const LitSpan& cond) {
    startDir(Directive_t::Output).add(str).add(cond).endDir();
}
void AspifOutput::external(Atom_t a, Value_t v) {
    startDir(Directive_t::External).add(static_cast<int>(a)).add(static_cast<int>(v)).endDir();
}
void AspifOutput::assume(const LitSpan& lits) { startDir(Directive_t::Assume).add(lits).endDir(); }
void AspifOutput::project(const AtomSpan& atoms) { startDir(Directive_t::Project).add(atoms).endDir(); }
void AspifOutput::acycEdge(int s, int t, const LitSpan& cond) {
    startDir(Directive_t::Edge).add(s).add(t).add(cond).endDir();
}
void AspifOutput::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& cond) {
    startDir(Directive_t::Heuristic)
        .add(static_cast<int>(t))
        .add(static_cast<int>(a))
        .add(bias)
        .add(static_cast<int>(prio))
        .add(cond)
        .endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int number) {
    startDir(Directive_t::Theory).add(uint32_t(Theory_t::Number)).add(termId).add(number).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, const std::string_view& name) {
    startDir(Directive_t::Theory).add(uint32_t(Theory_t::Symbol)).add(termId).add(name).endDir();
}
void AspifOutput::theoryTerm(Id_t termId, int cId, const IdSpan& args) {
    startDir(Directive_t::Theory).add(uint32_t(Theory_t::Compound)).add(termId).add(cId).add(args).endDir();
}
void AspifOutput::theoryElement(Id_t elementId, const IdSpan& terms, const LitSpan& cond) {
    startDir(Directive_t::Theory).add(uint32_t(Theory_t::Element)).add(elementId).add(terms).add(cond).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) {
    startDir(Directive_t::Theory).add(uint32_t(Theory_t::Atom)).add(atomOrZero).add(termId).add(elements).endDir();
}
void AspifOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) {
    startDir(Directive_t::Theory)
        .add(uint32_t(Theory_t::AtomWithGuard))
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
