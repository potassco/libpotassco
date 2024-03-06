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

#include <potassco/string_convert.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
using SymTab = std::unordered_map<Potassco::Atom_t, const char*>;
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert::SmData
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsConvert::SmData {
    struct Atom {
        Atom() : smId(0), head(0), show(0), extn(0) {}
        operator Atom_t() const { return smId; }
        unsigned smId : 28; // corresponding smodels atom
        unsigned head : 1;  // atom occurs in a head of a rule
        unsigned show : 1;  // atom has a name
        unsigned extn : 2;  // value if atom is external
    };
    struct Heuristic {
        Atom_t      atom;
        Heuristic_t type;
        int         bias{};
        unsigned    prio{};
        unsigned    cond{};
    };
    struct Symbol {
        unsigned    atom : 31;
        unsigned    hash : 1;
        const char* name{};
        bool        operator<(const Symbol& rhs) const { return atom < rhs.atom; }
    };
    using AtomMap = std::vector<Atom>;
    using AtomVec = std::vector<Atom_t>;
    using LitVec  = std::vector<Lit_t>;
    using WLitVec = std::vector<WeightLit_t>;
    using HeuVec  = std::vector<Heuristic>;
    using MinMap  = std::map<Weight_t, WLitVec>;
    using OutVec  = std::vector<Symbol>;
    SmData() : next_(2) {}
    ~SmData() {
        flushStep();
        for (auto& [_, value] : symTab_) { delete[] value; }
    }
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
    AtomSpan mapHead(const AtomSpan& h);
    template <class T>
    auto mapLits(const std::span<const T>& in, std::vector<T>& out) -> std::span<const T> {
        out.clear();
        for (const auto& x : in) { out.push_back(mapLit(x)); }
        return out;
    }
    const char* addOutput(Atom_t atom, const std::string_view&, bool addHash);
    void        addMinimize(Weight_t prio, const WeightLitSpan& lits) {
        WLitVec& body = minimize_[prio];
        body.reserve(body.size() + size(lits));
        for (auto x : lits) {
            if (weight(x) < 0) {
                x.lit    = -x.lit;
                x.weight = -x.weight;
            }
            body.push_back(x);
        }
    }
    void addExternal(Atom_t a, Value_t v) {
        Atom& ma = mapAtom(a);
        if (!ma.head) {
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
        for (; !output_.empty(); output_.pop_back()) {
            if (!output_.back().hash) {
                delete[] output_.back().name;
            }
        }
    }
    AtomMap     atoms_;     // maps input atoms to output atoms
    MinMap      minimize_;  // maps priorities to minimize statements
    AtomVec     head_;      // active rule head
    LitVec      lits_;      // active body literals
    WLitVec     wlits_;     // active weight body literals
    AtomVec     extern_;    // external atoms
    HeuVec      heuristic_; // list of heuristic modifications not yet processed
    SymTab      symTab_;
    OutVec      output_; // list of output atoms not yet processed
    Atom_t      next_;   // next unused output atom
    std::string strBuffer_;
};
AtomSpan SmodelsConvert::SmData::mapHead(const AtomSpan& h) {
    head_.clear();
    for (auto a : h) { head_.push_back(mapHeadAtom(a)); }
    if (head_.empty()) {
        head_.push_back(falseAtom());
    }
    return head_;
}
const char* SmodelsConvert::SmData::addOutput(Atom_t atom, const std::string_view& str, bool addHash) {
    char* n                             = new char[str.size() + 1];
    *std::copy(begin(str), end(str), n) = 0;
    Symbol s{.atom = atom, .hash = 0, .name = n};
    if (addHash && symTab_.insert(SymTab::value_type(atom, s.name)).second) {
        s.hash = 1;
    }
    output_.push_back(s);
    return s.name;
}
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert
/////////////////////////////////////////////////////////////////////////////////////////
SmodelsConvert::SmodelsConvert(AbstractProgram& out, bool ext) : out_(out), data_(new SmData), ext_(ext) {}
SmodelsConvert::~SmodelsConvert() { delete data_; }
Lit_t       SmodelsConvert::get(Lit_t in) const { return data_->mapLit(in); }
unsigned    SmodelsConvert::maxAtom() const { return data_->next_ - 1; }
const char* SmodelsConvert::getName(Atom_t a) const {
    auto it = data_->symTab_.find(a);
    return it != data_->symTab_.end() ? it->second : 0;
}
Atom_t SmodelsConvert::makeAtom(const LitSpan& cond, bool named) {
    Atom_t id = 0;
    if (size(cond) != 1 || cond[0] < 0 || (data_->mapAtom(atom(cond[0])).show && named)) {
        // aux :- cond.
        Atom_t aux = (id = data_->newAtom());
        out_.rule(Head_t::Disjunctive, {&aux, 1}, data_->mapLits(cond, data_->lits_));
    }
    else {
        SmData::Atom& ma = data_->mapAtom(atom(*begin(cond)));
        ma.show          = static_cast<unsigned>(named);
        id               = ma.smId;
    }
    return id;
}
void SmodelsConvert::initProgram(bool inc) { out_.initProgram(inc); }
void SmodelsConvert::beginStep() { out_.beginStep(); }
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    if (!empty(head) || ht == Head_t::Disjunctive) {
        AtomSpan mHead = data_->mapHead(head);
        out_.rule(ht, mHead, data_->mapLits(body, data_->lits_));
    }
}
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (!empty(head) || ht == Head_t::Disjunctive) {
        AtomSpan      mHead = data_->mapHead(head);
        WeightLitSpan mBody = data_->mapLits(body, data_->wlits_);
        if (isSmodelsRule(ht, mHead, bound, mBody)) {
            out_.rule(ht, mHead, bound, mBody);
            return;
        }
        Atom_t aux = data_->newAtom();
        data_->lits_.assign(1, lit(aux));
        out_.rule(Head_t::Disjunctive, {&aux, 1}, bound, mBody);
        out_.rule(ht, mHead, data_->lits_);
    }
}

void SmodelsConvert::minimize(Weight_t prio, const WeightLitSpan& lits) { data_->addMinimize(prio, lits); }
void SmodelsConvert::output(const std::string_view& str, const LitSpan& cond) {
    // create a unique atom for cond and set its name to str
    data_->addOutput(makeAtom(cond, true), str, true);
}

void SmodelsConvert::external(Atom_t a, Value_t v) { data_->addExternal(a, v); }
void SmodelsConvert::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& cond) {
    if (!ext_) {
        out_.heuristic(a, t, bias, prio, cond);
    }
    // create unique atom representing _heuristic(...)
    Atom_t heuPred = makeAtom(cond, true);
    data_->addHeuristic(a, t, bias, prio, heuPred);
}
void SmodelsConvert::acycEdge(int s, int t, const LitSpan& condition) {
    if (!ext_) {
        out_.acycEdge(s, t, condition);
    }
    data_->strBuffer_.clear();
    data_->addOutput(makeAtom(condition, true), formatTo(data_->strBuffer_, "_edge({},{})", s, t), false);
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
    for (const auto& [prio, lits] : data_->minimize_) {
        out_.minimize(prio, data_->mapLits(std::span{lits}, data_->wlits_));
    }
}
void SmodelsConvert::flushExternal() {
    LitSpan T{};
    data_->head_.clear();
    for (auto ext : data_->extern_) {
        SmData::Atom& a  = data_->mapAtom(ext);
        auto          vt = static_cast<Value_t>(a.extn);
        if (!ext_) {
            if (a.head) {
                continue;
            }
            Atom_t at = a;
            if (vt == Value_t::Free) {
                data_->head_.push_back(at);
            }
            else if (vt == Value_t::True) {
                out_.rule(Head_t::Disjunctive, {&at, 1}, T);
            }
        }
        else {
            out_.external(a, vt);
        }
    }
    if (!data_->head_.empty()) {
        out_.rule(Head_t::Choice, data_->head_, T);
    }
}
void SmodelsConvert::flushHeuristic() {
    for (const auto& heu : data_->heuristic_) {
        if (!data_->mapped(heu.atom)) {
            continue;
        }
        SmData::Atom& ma   = data_->mapAtom(heu.atom);
        const char*   name = ma.show ? getName(ma.smId) : nullptr;
        if (!name) {
            ma.show = 1;
            data_->strBuffer_.clear();
            name = data_->addOutput(ma, formatTo(data_->strBuffer_, "_atom({})", ma.smId), true);
        }
        data_->strBuffer_.clear();
        formatTo(data_->strBuffer_, "_heuristic({},{},{},{})", name, toString(heu.type).c_str(), heu.bias, heu.prio);
        auto c = static_cast<Lit_t>(heu.cond);
        out_.output(data_->strBuffer_, {&c, 1});
    }
}
void SmodelsConvert::flushSymbols() {
    std::sort(data_->output_.begin(), data_->output_.end());
    for (const auto& sym : data_->output_) {
        auto x = static_cast<Lit_t>(sym.atom);
        out_.output(sym.name, {&x, 1});
    }
}
} // namespace Potassco
