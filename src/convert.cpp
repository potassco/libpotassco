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
#include <potassco/convert.h>

#include <potassco/error.h>
#include <potassco/rule_utils.h>
#include <potassco/smodels.h>

POTASSCO_WARNING_BEGIN_RELAXED
#include <amc/vector.hpp>
POTASSCO_WARNING_END_RELAXED

#include <algorithm>
#include <charconv>
#include <memory>
#include <string_view>
#include <unordered_map>

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
namespace Potassco {
using namespace std::literals;
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert::SmData
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsConvert::SmData {
    using ScratchType = DynamicBuffer;
    template <std::integral T>
    static void append(ScratchType& scratch, T in) {
        char tmp[std::numeric_limits<T>::digits10 + 1];
        auto sz = std::to_chars(tmp, std::end(tmp), in).ptr - tmp;
        scratch.append(tmp, static_cast<std::size_t>(sz));
    }
    static void append(ScratchType& scratch, std::string_view n) { scratch.append(n.data(), n.size()); }
    template <typename... Args>
    static std::string_view makePred(ScratchType& scratch, std::string_view name, Args... args) {
        static_assert(sizeof...(Args) > 0, "at least one arg expected");
        scratch.clear();
        scratch.append(name.data(), name.size());
        scratch.push('(');
        ((append(scratch, args), scratch.push(',')), ...);
        scratch.back() = ')';
        return scratch.view();
    }
    struct Atom {
        Atom() : smId(0), head(0), show(0), extn(0) {}
        std::string_view makePred(ScratchType& scratch) const { return SmData::makePred(scratch, "_atom"sv, smId); }
        operator Atom_t() const { return smId; }
        uint32_t smId : 28; // corresponding smodels atom
        uint32_t head : 1;  // atom occurs in a head of a rule
        uint32_t show : 1;  // atom has a name
        uint32_t extn : 2;  // value if atom is external
    };
    struct Heuristic {
        std::string_view makePred(ScratchType& scratch, std::string_view atomName) const {
            return SmData::makePred(scratch, "_heuristic"sv, atomName, enum_name(type), bias, prio);
        }
        Atom_t      atom;
        Heuristic_t type;
        int         bias{};
        unsigned    prio{};
        unsigned    cond{};
    };
    using SymTab = std::unordered_map<Atom_t, FixedString>;
    struct Output {
        enum Type : uint8_t { Name = 0, Edge = 1 };
        struct EdgeT {
            int32_t s;
            int32_t t;
        };
        Output(SymTab::iterator it) : atom(it->first), type(Name), name(&it->second) {}
        Output(Atom_t a, int32_t s, int32_t t) : atom(a), type(Edge), edge{s, t} {}
        std::string_view makePred(ScratchType& scratch) const {
            if (type == Name)
                return name->view();
            else
                return SmData::makePred(scratch, "_edge"sv, edge.s, edge.t);
        }
        uint32_t atom : 31;
        uint32_t type : 1;
        union {
            const FixedString* name{};
            EdgeT              edge;
        };
    };
    static_assert(amc::is_trivially_relocatable_v<Atom> && amc::is_trivially_relocatable_v<Atom_t> &&
                  amc::is_trivially_relocatable_v<Lit_t> && amc::is_trivially_relocatable_v<WeightLit_t> &&
                  amc::is_trivially_relocatable_v<Heuristic> && amc::is_trivially_relocatable_v<Output>);
    using AtomMap = amc::vector<Atom>;
    using AtomVec = amc::vector<Atom_t>;
    using WLitVec = amc::vector<WeightLit_t>;
    using HeuVec  = amc::vector<Heuristic>;
    using OutVec  = amc::vector<Output>;
    struct Minimize {
        static_assert(amc::is_trivially_relocatable_v<WLitVec>);
        using trivially_relocatable = std::true_type;
        Weight_t prio;
        WLitVec  lits;
    };
    static_assert(amc::is_trivially_relocatable_v<Minimize>);
    using MinSet = amc::vector<Minimize>;
    SmData() : next_(2) {}
    Atom_t newAtom() { return next_++; }
    Atom_t falseAtom() { return 1; }
    bool   mapped(Atom_t a) const { return a < atoms_.size() && atoms_[a].smId != 0; }
    Atom&  mapAtom(Atom_t a) {
        if (mapped(a)) {
            return atoms_[a];
        }
        if (a >= atoms_.size()) {
            atoms_.resize(a + 1);
        }
        atoms_[a].smId = next_++;
        return atoms_[a];
    }
    Lit_t mapLit(Lit_t in) {
        auto x = static_cast<Lit_t>(mapAtom(atom(in)));
        return in < 0 ? -x : x;
    }
    WeightLit_t mapLit(WeightLit_t in) {
        in.lit = mapLit(in.lit);
        return in;
    }
    Atom_t mapHeadAtom(Atom_t a) {
        Atom& x = mapAtom(a);
        x.head  = 1;
        return x;
    }
    RuleBuilder& mapHead(const AtomSpan& h, Head_t ht = Head_t::Disjunctive) {
        rule_.clear().start(ht);
        for (auto a : h) { rule_.addHead(mapHeadAtom(a)); }
        if (h.empty()) {
            rule_.addHead(falseAtom());
        }
        return rule_;
    }
    template <class T>
    RuleBuilder& mapBody(const std::span<const T>& in) {
        for (const auto& x : in) { rule_.addGoal(mapLit(x)); }
        return rule_;
    }
    SymTab::iterator addOutput(Atom_t atom, const std::string_view& str) {
        auto [it, added] = symTab_.try_emplace(atom, str);
        POTASSCO_CHECK_PRE(added, "Redefinition: atom '%u:%.*s' already shown as '%s'", atom, int(str.size()),
                           str.data(), it->second.c_str());
        output_.emplace_back(it);
        return it;
    }

    void addMinimize(Weight_t prio, const WeightLitSpan& lits) {
        auto it = std::lower_bound(minimize_.begin(), minimize_.end(), prio,
                                   [](const Minimize& lhs, Weight_t rhs) { return lhs.prio < rhs; });
        if (it == minimize_.end() || it->prio != prio)
            it = minimize_.insert(it, {.prio = prio, .lits = {}});
        for (auto x : lits) {
            if (weight(x) < 0) {
                x.lit    = -x.lit;
                x.weight = -x.weight;
            }
            it->lits.push_back(x);
        }
    }
    void addExternal(Atom_t a, Value_t v) {
        if (auto& ma = mapAtom(a); not ma.head) {
            ma.extn = static_cast<unsigned>(v);
            extern_.push_back(a);
        }
    }
    void addHeuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, Atom_t cond) {
        Heuristic h = {a, t, bias, prio, cond};
        heuristic_.push_back(h);
    }
    void flushStep() {
        minimize_.clear();
        AtomVec().swap(extern_);
        HeuVec().swap(heuristic_);
        output_.clear();
    }
    AtomMap     atoms_;     // maps input atoms to output atoms
    SymTab      symTab_;    // maps output atoms to their names
    AtomVec     extern_;    // external atoms
    HeuVec      heuristic_; // list of heuristic modifications not yet processed
    MinSet      minimize_;  // set of minimize literals
    OutVec      output_;    // list of output atoms not yet processed
    RuleBuilder rule_;      // active (mapped) rule
    Atom_t      next_;      // next unused output atom
};
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert
/////////////////////////////////////////////////////////////////////////////////////////
SmodelsConvert::SmodelsConvert(AbstractProgram& out, bool ext)
    : out_(out)
    , data_(std::make_unique<SmData>())
    , ext_(ext) {}
SmodelsConvert::~SmodelsConvert() = default;
Lit_t    SmodelsConvert::get(Lit_t in) const { return data_->mapLit(in); }
unsigned SmodelsConvert::maxAtom() const { return data_->next_ - 1; }
Atom_t   SmodelsConvert::makeAtom(const LitSpan& cond, bool named) {
    Atom_t id = 0;
    if (cond.size() != 1 || cond[0] < 0 || (data_->mapAtom(atom(cond[0])).show && named)) {
        // aux :- cond.
        data_->rule_.clear().addHead(id = data_->newAtom());
        data_->mapBody(cond).end(&out_);
    }
    else {
        SmData::Atom& ma = data_->mapAtom(atom(cond.front()));
        ma.show          = static_cast<unsigned>(named);
        id               = ma.smId;
    }
    return id;
}
void SmodelsConvert::initProgram(bool inc) { out_.initProgram(inc); }
void SmodelsConvert::beginStep() { out_.beginStep(); }
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    if (not head.empty() || ht == Head_t::Disjunctive) {
        data_->mapHead(head, ht).startBody();
        data_->mapBody(body).end(&out_);
    }
}
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (not head.empty() || ht == Head_t::Disjunctive) {
        data_->mapHead(head, ht).startSum(bound);
        data_->mapBody(body);
        auto mHead = data_->rule_.head();
        auto mBody = data_->rule_.sum().lits;
        if (isSmodelsRule(ht, mHead, bound, mBody)) {
            data_->rule_.end(&out_);
            return;
        }
        auto auxH = data_->newAtom();
        auto auxB = lit(auxH);
        POTASSCO_CHECK(isSmodelsRule(Head_t::Disjunctive, {&auxH, 1}, bound, mBody), Errc::invalid_argument,
                       "unsupported rule");
        out_.rule(Head_t::Disjunctive, {&auxH, 1}, bound, mBody);
        out_.rule(ht, mHead, {&auxB, 1});
    }
}

void SmodelsConvert::minimize(Weight_t prio, const WeightLitSpan& lits) { data_->addMinimize(prio, lits); }
void SmodelsConvert::output(const std::string_view& str, const LitSpan& cond) {
    // create a unique atom for cond and set its name to str
    data_->addOutput(makeAtom(cond, true), str);
}

void SmodelsConvert::external(Atom_t a, Value_t v) { data_->addExternal(a, v); }
void SmodelsConvert::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& cond) {
    if (not ext_) {
        out_.heuristic(a, t, bias, prio, cond);
    }
    // create unique atom representing _heuristic(...)
    Atom_t heuPred = makeAtom(cond, true);
    data_->addHeuristic(a, t, bias, prio, heuPred);
}
void SmodelsConvert::acycEdge(int s, int t, const LitSpan& condition) {
    if (not ext_) {
        out_.acycEdge(s, t, condition);
    }
    data_->output_.emplace_back(makeAtom(condition, true), s, t);
}

void SmodelsConvert::flush() {
    flushMinimize();
    flushExternal();
    flushHeuristic();
    flushSymbols();
    Lit_t f = -static_cast<Lit_t>(data_->falseAtom());
    out_.assume({&f, 1});
    data_->flushStep();
}
void SmodelsConvert::endStep() {
    flush();
    out_.endStep();
}
void SmodelsConvert::flushMinimize() {
    const SmData::Minimize* last = nullptr;
    for (const auto& m : data_->minimize_) {
        assert(not last || last->prio < m.prio);
        data_->rule_.clear().startMinimize(m.prio);
        data_->mapBody(std::span{m.lits}).end(&out_);
        last = &m;
    }
}
void SmodelsConvert::flushExternal() {
    LitSpan T{};
    data_->rule_.clear();
    for (auto ext : data_->extern_) {
        SmData::Atom& a  = data_->mapAtom(ext);
        auto          vt = static_cast<Value_t>(a.extn);
        if (not ext_) {
            if (a.head) {
                continue;
            }
            Atom_t at = a;
            if (vt == Value_t::Free) {
                data_->rule_.addHead(at);
            }
            else if (vt == Value_t::True) {
                out_.rule(Head_t::Disjunctive, {&at, 1}, T);
            }
        }
        else {
            out_.external(a, vt);
        }
    }
    if (auto head = data_->rule_.head(); not head.empty()) {
        out_.rule(Head_t::Choice, head, T);
    }
}
void SmodelsConvert::flushHeuristic() {
    if (data_->heuristic_.empty())
        return;
    SmData::ScratchType scratch;
    for (const auto& heu : data_->heuristic_) {
        if (not data_->mapped(heu.atom)) {
            continue;
        }
        SmData::Atom& ma = data_->mapAtom(heu.atom);
        auto          it = ma.show ? data_->symTab_.find(ma.smId) : data_->symTab_.end();
        if (it == data_->symTab_.end()) {
            ma.show = 1;
            it      = data_->addOutput(ma, ma.makePred(scratch));
        }
        auto c = static_cast<Lit_t>(heu.cond);
        out_.output(heu.makePred(scratch, it->second.view()), {&c, 1});
    }
}
void SmodelsConvert::flushSymbols() {
    SmData::ScratchType scratch;
    std::sort(data_->output_.begin(), data_->output_.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.atom < rhs.atom; });
    for (const auto& sym : data_->output_) {
        auto x = static_cast<Lit_t>(sym.atom);
        out_.output(sym.makePred(scratch), {&x, 1});
    }
}
} // namespace Potassco
