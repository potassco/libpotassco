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
#pragma once

#include <potassco/basic_types.h>
#include <potassco/error.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Potassco::Test {
template <class T>
using Vec = std::vector<T>;
struct AtomicCond {
    explicit AtomicCond(Atom_t& atom) : r(&atom) {}
    operator LitSpan() const { return cond(); }       // NOLINT
    operator Vec<Lit_t>() const { return {lit(*r)}; } // NOLINT
    [[nodiscard]] auto cond() const -> LitSpan { return LitSpan{reinterpret_cast<Lit_t*>(r), 1}; }
    Atom_t*            r;

    friend bool operator==(AtomicCond lhs, LitSpan rhs) { return rhs.size() == 1 && rhs.front() == lit(*lhs.r); }
    friend bool operator==(AtomicCond lhs, const Vec<Lit_t>& rhs) { return lhs == LitSpan{rhs}; }
};
inline auto toCond(Atom_t& atom) { return AtomicCond{atom}; }
struct Rule {
    HeadType       ht;
    Vec<Atom_t>    head;
    BodyType       bt;
    Weight_t       bnd;
    Vec<WeightLit> body;
    bool           operator==(const Rule& rhs) const = default;
};
struct Edge {
    int        s;
    int        t;
    Vec<Lit_t> cond;
    bool       operator==(const Edge& rhs) const = default;
};
struct Heuristic {
    Atom_t      atom;
    DomModifier type;
    int         bias;
    unsigned    prio;
    Vec<Lit_t>  cond;
    bool        operator==(const Heuristic& rhs) const = default;
};

class ReadObserver : public AbstractProgram {
public:
    void initProgram(bool inc) override { incremental = inc; }
    void beginStep() override { ++nStep; }
    void endStep() override {}

    void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan cond) override {
        heuristics.push_back({a, t, bias, prio, {begin(cond), end(cond)}});
    }
    void acycEdge(int s, int t, LitSpan cond) override { edges.push_back({s, t, {begin(cond), end(cond)}}); }
    void outputAtom(Atom_t a, std::string_view str) override {
        POTASSCO_CHECK_PRE(a || allowZeroAtom, "invalid atom");
        if (auto& s = atoms[lit(a)]; s.empty()) {
            s = str;
        }
        else {
            s.append(1, ';').append(str);
        }
    }

    using AtomMap = std::unordered_map<int, std::string>;
    AtomMap        atoms;
    Vec<Heuristic> heuristics;
    Vec<Edge>      edges;
    int            nStep         = 0;
    bool           incremental   = false;
    bool           allowZeroAtom = false;
};
} // namespace Potassco::Test
