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

#include "test_common.h"

#include <potassco/convert.h>
#include <potassco/smodels.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <unordered_map>

namespace Potassco::Test::Smodels {
using AtomTab = std::vector<std::string>;
using LitVec  = std::vector<Lit_t>;
using RawRule = std::vector<int>;
static void finalize(std::stringstream& str, const AtomTab& atoms = AtomTab(), const std::string& bpos = "",
                     const std::string& bneg = "1") {
    str << "0\n";
    for (const auto& s : atoms) str << s << "\n";
    str << "0\nB+\n" << bpos;
    if (not bpos.empty() && bpos.back() != '\n')
        str << "\n";
    str << "0\nB-\n";
    str << bneg;
    if (not bneg.empty() && bneg.back() != '\n')
        str << "\n";
    str << "0\n1\n";
}

using Rule_t = SmodelsRule_t;

class ReadObserver : public Test::ReadObserver {
public:
    void rule(Head_t ht, const AtomSpan& head, const LitSpan& body) override {
        if (head.empty()) {
            if (ht == Head_t::Choice)
                return;
            compute.push_back(-lit(body[0]));
        }
        else {
            Rule_t           rt = Rule_t::Basic;
            std::vector<int> r(1, lit(head[0]));
            if (size(head) > 1 || ht == Head_t::Choice) {
                r[0] = static_cast<int>(size(head));
                r.insert(r.end(), begin(head), end(head));
                rt = ht == Head_t::Choice ? Rule_t::Choice : Rule_t::Disjunctive;
            }
            r.insert(r.end(), begin(body), end(body));
            rules[rt].push_back(std::move(r));
        }
    }
    void rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) override {
        REQUIRE(size(head) == 1);
        REQUIRE(ht == Head_t::Disjunctive);
        std::vector<int> r(1, lit(head[0]));
        r.push_back(bound);
        auto hasWeights = std::ranges::any_of(body, [](const auto& wl) { return weight(wl) != 1; });
        for (auto&& x : body) {
            REQUIRE(weight(x) >= 0);
            r.push_back(lit(x));
            if (hasWeights) {
                r.push_back(weight(x));
            }
        }
        rules[hasWeights ? Rule_t::Weight : Rule_t::Cardinality].push_back(std::move(r));
    }
    void minimize(Weight_t prio, const WeightLitSpan& lits) override {
        std::vector<int> r;
        r.reserve((size(lits) * 2) + 1);
        r.push_back(prio);
        for (auto&& x : lits) {
            r.push_back(lit(x));
            r.push_back(weight(x));
        }
        rules[Rule_t::Optimize].push_back(std::move(r));
    }
    void project(const AtomSpan&) override {}
    void output(const std::string_view& str, const LitSpan& cond) override {
        REQUIRE(size(cond) == 1);
        atoms[*begin(cond)].assign(begin(str), end(str));
    }

    void external(Atom_t a, Value_t v) override {
        if (v != Value_t::Release) {
            rules[Rule_t::ClaspAssignExt].push_back(RawRule{static_cast<int>(a), static_cast<int>(v)});
        }
        else {
            rules[Rule_t::ClaspReleaseExt].push_back(RawRule{static_cast<int>(a)});
        }
    }
    void assume(const LitSpan&) override {}

    using RuleMap = std::map<Rule_t, std::vector<std::vector<int>>>;
    using AtomMap = std::unordered_map<int, std::string>;
    RuleMap rules;
    LitVec  compute;
    AtomMap atoms;
};
TEST_CASE("Smodels reader ", "[smodels]") {
    std::stringstream input;
    ReadObserver      observer;
    SECTION("read empty") {
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.nStep == 1);
        REQUIRE(observer.incremental == false);
        REQUIRE(observer.compute.size() == 1);
        REQUIRE(observer.compute[0] == -1);
    }
    SECTION("read basic rule") {
        input << "1 2 4 2 4 2 3 5\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        RawRule exp = {2, -4, -2, 3, 5};
        REQUIRE(observer.rules[Rule_t::Basic][0] == exp);
    }
    SECTION("read choice/disjunctive rule") {
        input << "3 2 3 4 2 1 5 6\n";
        input << "8 2 3 4 2 1 5 6\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Choice].size() == 1);
        REQUIRE(observer.rules[Rule_t::Disjunctive].size() == 1);

        RawRule r1 = {2, 3, 4, -5, 6};
        REQUIRE(observer.rules[Rule_t::Choice][0] == r1);
        REQUIRE(observer.rules[Rule_t::Disjunctive][0] == r1);
    }
    SECTION("read card/weight rule") {
        input << "2 2 2 1 1 3 4\n";
        input << "5 3 3 3 1 4 5 6 1 2 3\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Cardinality].size() == 1);
        REQUIRE(observer.rules[Rule_t::Weight].size() == 1);

        RawRule r1 = {2, 1, -3, 4};
        RawRule r2 = {3, 3, -4, 1, 5, 2, 6, 3};
        REQUIRE(observer.rules[Rule_t::Cardinality][0] == r1);
        REQUIRE(observer.rules[Rule_t::Weight][0] == r2);
    }
    SECTION("read min rule") {
        input << "6 0 3 1 4 5 6 1 3 2\n";
        input << "6 0 3 1 4 5 6 1 3 2\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Optimize].size() == 2);
        RawRule r = {0, -4, 1, 5, 3, 6, 2};
        REQUIRE(observer.rules[Rule_t::Optimize][0] == r);
        r[0] = 1;
        REQUIRE(observer.rules[Rule_t::Optimize][1] == r);
    }
    SECTION("read atom names") {
        finalize(input, {"1 Foo", "2 Bar", "3 Test(X,Y)"});
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.atoms.size() == 3);
        REQUIRE(observer.atoms[1] == "Foo");
        REQUIRE(observer.atoms[2] == "Bar");
        REQUIRE(observer.atoms[3] == "Test(X,Y)");
    }
    SECTION("read compute") {
        finalize(input, {}, "2\n3", "1\n4\n5");
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.compute.size() == 5);
        const auto& c = observer.compute;
        REQUIRE(std::find(c.begin(), c.end(), 2) != c.end());
        REQUIRE(std::find(c.begin(), c.end(), 3) != c.end());
        REQUIRE(std::find(c.begin(), c.end(), -1) != c.end());
        REQUIRE(std::find(c.begin(), c.end(), -4) != c.end());
        REQUIRE(std::find(c.begin(), c.end(), -5) != c.end());
    }
    SECTION("read external") {
        input << "0\n0\nB+\n0\nB-\n0\nE\n1\n2\n4\n0\n1\n";
        REQUIRE(Potassco::readSmodels(input, observer) == 0);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt].size() == 3);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][0] == RawRule({1, 0}));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][1] == RawRule({2, 0}));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][2] == RawRule({4, 0}));
    }
}

TEST_CASE("SmodelsExtReader ", "[smodels][smodels_ext]") {
    std::stringstream     input;
    ReadObserver          observer;
    SmodelsInput::Options opts;
    opts.enableClaspExt();
    SECTION("read incremental") {
        input << "90 0\n";
        finalize(input);
        input << "90 0\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer, opts) == 0);
        REQUIRE(observer.incremental == true);
        REQUIRE(observer.nStep == 2);
    }
    SECTION("read external") {
        input << "91 2 0\n";
        input << "91 3 1\n";
        input << "91 4 2\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer, opts) == 0);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt].size() == 3);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][0] == RawRule({2, static_cast<int>(Value_t::False)}));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][1] == RawRule({3, static_cast<int>(Value_t::True)}));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][2] == RawRule({4, static_cast<int>(Value_t::Free)}));
    }
    SECTION("read release") {
        input << "91 2 0\n";
        input << "92 2\n";
        finalize(input);
        REQUIRE(Potassco::readSmodels(input, observer, opts) == 0);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt].size() == 1);
        REQUIRE(observer.rules[Rule_t::ClaspReleaseExt].size() == 1);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][0] == RawRule({2, static_cast<int>(Value_t::False)}));
        REQUIRE(observer.rules[Rule_t::ClaspReleaseExt][0] == RawRule({2}));
    }
}

TEST_CASE("Write smodels", "[smodels]") {
    std::stringstream str, exp;
    SmodelsOutput     writer(str, false, 0);
    ReadObserver      observer;
    writer.initProgram(false);
    SECTION("empty program is valid") {
        writer.endStep();
        exp << "0\n0\nB+\n0\nB-\n0\n1\n";
        REQUIRE(str.str() == exp.str());
    }
    SECTION("constraints require false atom") {
        REQUIRE_THROWS_AS(writer.rule(Head_t::Disjunctive, {}, {}), std::logic_error);
        REQUIRE_NOTHROW(writer.rule(Head_t::Choice, {}, {}));
        writer.endStep();
        exp << "0\n0\nB+\n0\nB-\n0\n1\n";
        REQUIRE(str.str() == exp.str());
        str.clear();
        str.str("");
        SmodelsOutput withFalse(str, false, 1);
        Vec<Lit_t>    body = {2, -3, -4, 5};
        REQUIRE_NOTHROW(withFalse.rule(Head_t::Disjunctive, {}, body));
        Vec<WeightLit_t> wbody = {{2, 2}, {-3, 1}, {-4, 3}, {5, 4}};
        REQUIRE_NOTHROW(withFalse.rule(Head_t::Disjunctive, {}, 2, wbody));
        REQUIRE(str.str().find("1 1 4 2 3 4 2 5") != std::string::npos);
        REQUIRE(str.str().find("5 1 2 4 2 3 4 2 5 1 3 2 4") != std::string::npos);
    }
    SECTION("body literals are correctly reordered") {
        Atom_t     a    = 1;
        Vec<Lit_t> body = {2, -3, -4, 5};
        writer.rule(Head_t::Disjunctive, {&a, 1}, body);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        RawRule r = {1, -3, -4, 2, 5};
        REQUIRE(observer.rules[Rule_t::Basic][0] == r);
    }
    SECTION("body literals with weights are correctly reordered") {
        Atom_t           a    = 1;
        Vec<WeightLit_t> body = {{2, 2}, {-3, 1}, {-4, 3}, {5, 4}};
        writer.rule(Head_t::Disjunctive, {&a, 1}, 4, body);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Weight].size() == 1);
        RawRule r = {1, 4, -3, 1, -4, 3, 2, 2, 5, 4};
        REQUIRE(observer.rules[Rule_t::Weight][0] == r);
    }
    SECTION("weights are removed from count bodies") {
        Atom_t           a    = 1;
        Vec<WeightLit_t> body = {{2, 1}, {-3, 1}, {-4, 1}, {5, 1}};
        writer.rule(Head_t::Disjunctive, {&a, 1}, 3, body);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Cardinality].size() == 1);
        RawRule r = {1, 3, -3, -4, 2, 5};
        REQUIRE(observer.rules[Rule_t::Cardinality][0] == r);
    }
    SECTION("all head atoms are written") {
        Vec<Atom_t> atoms = {1, 2, 3, 4};
        writer.rule(Head_t::Disjunctive, atoms, {});
        writer.rule(Head_t::Choice, atoms, {});
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Disjunctive].size() == 1);
        REQUIRE(observer.rules[Rule_t::Choice].size() == 1);
        RawRule r = {4, 1, 2, 3, 4};
        REQUIRE(observer.rules[Rule_t::Disjunctive][0] == r);
        REQUIRE(observer.rules[Rule_t::Choice][0] == r);
    }
    SECTION("minimize rules are written without priority") {
        Vec<WeightLit_t> body = {{2, 2}, {-3, 1}, {-4, 3}, {5, 4}};
        writer.minimize(10, body);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Optimize].size() == 1);
        RawRule r = {0, -3, 1, -4, 3, 2, 2, 5, 4};
        REQUIRE(observer.rules[Rule_t::Optimize][0] == r);
    }
    SECTION("minimize lits with negative weights are inverted") {
        Vec<WeightLit_t> body = {{2, -2}, {3, 1}, {4, -1}};
        writer.minimize(10, body);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Optimize].size() == 1);
        RawRule r = {0, -2, 2, -4, 1, 3, 1};
        REQUIRE(observer.rules[Rule_t::Optimize][0] == r);
    }
    SECTION("output is written to symbol table") {
        Lit_t       a  = 1;
        std::string an = "Hallo";
        writer.output(an, {&a, 1});
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.atoms[a] == an);
    }
    SECTION("output must be added after rules") {
        Lit_t       a  = 1;
        std::string an = "Hallo";
        writer.output(an, {&a, 1});
        Atom_t b = 2;
        REQUIRE_THROWS(writer.rule(Head_t::Disjunctive, {&b, 1}, {}));
    }
    SECTION("compute statement is written via assume") {
        Atom_t     a    = 1;
        Vec<Lit_t> body = {2, -3, -4, 5};
        writer.rule(Head_t::Disjunctive, {&a, 1}, body);
        Lit_t na = -1;
        writer.assume({&na, 1});
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        RawRule r = {1, -3, -4, 2, 5};
        REQUIRE(observer.rules[Rule_t::Basic][0] == r);
        REQUIRE(observer.compute.size() == 1);
        REQUIRE(observer.compute[0] == na);
    }
    SECTION("compute statement can contain multiple literals") {
        Vec<Lit_t> compute = {-1, 2, -3, -4, 5, 6};
        writer.assume(compute);
        writer.endStep();
        REQUIRE(readSmodels(str, observer) == 0);
        // B+ followed by B-
        std::stable_partition(compute.begin(), compute.end(), [](Lit_t x) { return x > 0; });
        REQUIRE(observer.compute == compute);
    }
    SECTION("only one compute statement supported") {
        Lit_t na = -1, b = 2;
        writer.assume({&na, 1});
        REQUIRE_THROWS(writer.assume({&b, 1}));
    }
    SECTION("complex directives are not supported") {
        REQUIRE_THROWS(writer.project({}));
        REQUIRE_THROWS(writer.external(1, Value_t::False));
        REQUIRE_THROWS(writer.heuristic(1, Heuristic_t::Sign, 1, 0, {}));
        REQUIRE_THROWS(writer.acycEdge(1, 2, {}));
    }
}

TEST_CASE("Match heuristic predicate", "[smodels]") {
    const char*      in;
    std::string_view atom;
    Heuristic_t      type;
    int              bias;
    unsigned         prio;
    SECTION("do not match invalid predicate name") {
        REQUIRE_FALSE(matchDomHeuPred(in = "heuristic()", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heu()", atom, type, bias, prio));
    }
    SECTION("do not match predicate with wrong arity") {
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,1,2,3)", atom, type, bias, prio));
    }
    SECTION("do not match predicate with invalid parameter") {
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,invalid,1)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,foo)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,1,-10)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,1a,-10)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign(),1)", atom, type, bias, prio));
    }
    SECTION("match _heuristic/3 and assign implicit priority") {
        REQUIRE(matchDomHeuPred(in = "_heuristic(x,level,-10)", atom, type, bias, prio));
        REQUIRE(prio == 10);
        REQUIRE(matchDomHeuPred(in = "_heuristic(x,sign,-2147483648)", atom, type, bias, prio));
        REQUIRE(bias == std::numeric_limits<int32_t>::min());
        REQUIRE(prio == static_cast<unsigned>(2147483647) + 1);
        REQUIRE(matchDomHeuPred(in = "_heuristic(x,sign,-3)", atom, type, bias, prio));
        REQUIRE(bias == -3);
        REQUIRE(prio == static_cast<unsigned>(3));
    }
    SECTION("match _heuristic/4") {
        REQUIRE(matchDomHeuPred(in = "_heuristic(x,level,-10,123)", atom, type, bias, prio));
        REQUIRE(bias == -10);
        REQUIRE(prio == 123);
    }
    SECTION("match complex atom name") {
        REQUIRE(matchDomHeuPred(in = "_heuristic(_heuristic(x,y,z),init,1)", atom, type, bias, prio));
        REQUIRE(type == Heuristic_t::Init);

        REQUIRE(matchDomHeuPred(in = "_heuristic(a(\"foo\"),init,1)", atom, type, bias, prio));
        REQUIRE(type == Heuristic_t::Init);
        REQUIRE(atom == R"(a("foo"))");

        REQUIRE(matchDomHeuPred(in = "_heuristic(a(\"fo\\\"o\"),init,1)", atom, type, bias, prio));
        REQUIRE(type == Heuristic_t::Init);
        REQUIRE(atom == "a(\"fo\\\"o\")");
    }
    SECTION("do not match out of bounds") {
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,-2147483649)", atom, type, bias, prio));
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(x,sign,2147483648)", atom, type, bias, prio));
    }
    SECTION("handle unterminated") {
        REQUIRE_FALSE(matchDomHeuPred(in = "_heuristic(a(\"foo,init,1)", atom, type, bias, prio));
    }
}

TEST_CASE("Match edge predicate", "[smodels]") {
    const char*      in;
    std::string_view v0;
    std::string_view v1;
    SECTION("do not match invalid predicate name") {
        REQUIRE_FALSE(matchEdgePred(in = "edge()", v0, v1));
        REQUIRE_FALSE(matchEdgePred(in = "_acyc()", v0, v1));
    }
    SECTION("do not match predicate with wrong arity") {
        REQUIRE_FALSE(matchEdgePred(in = "_edge(1)", v0, v1));
        REQUIRE_FALSE(matchEdgePred(in = "_edge(1,2,3)", v0, v1));

        REQUIRE_FALSE(matchEdgePred(in = "_acyc_1_2", v0, v1));
        REQUIRE_FALSE(matchEdgePred(in = "_acyc_1_2_3_4", v0, v1));
    }

    SECTION("do not match predicate with invalid parameter") {
        REQUIRE_FALSE(matchEdgePred(in = "_acyc_1_foo_bar", v0, v1));
        REQUIRE_FALSE(matchEdgePred(in = "_acyc_foo_1_2", v0, v1));
        REQUIRE_FALSE(matchEdgePred(in = "_acyc_1_1d_2y", v0, v1));
    }

    SECTION("match _acyc_/0") {
        REQUIRE(matchEdgePred(in = "_acyc_1_99_100", v0, v1));
        REQUIRE(v0 == "99");
        REQUIRE(v1 == "100");
        REQUIRE(matchEdgePred(in = "_acyc_1_-12_13", v0, v1));
        REQUIRE(v0 == "-12");
        REQUIRE(v1 == "13");
    }

    SECTION("match _edge/2") {
        REQUIRE(matchEdgePred(in = "_edge(x,y)", v0, v1));
        REQUIRE(v0 == "x");
        REQUIRE(v1 == "y");
        REQUIRE(matchEdgePred(in = "_edge(x(\"Foo,bar\"),bar)", v0, v1));
        REQUIRE(v0 == "x(\"Foo,bar\")");
        REQUIRE(v1 == "bar");
    }
}

TEST_CASE("SmodelsOutput supports extended programs", "[smodels_ext]") {
    std::stringstream str, exp;
    SmodelsOutput     out(str, true, 0);
    SmodelsConvert    writer(out, true);
    writer.initProgram(true);
    writer.beginStep();
    exp << "90 0\n";
    Vec<Atom_t>      head = {1, 2};
    Vec<Lit_t>       body = {3, -4};
    Vec<WeightLit_t> min  = {{-1, 2}, {2, 1}};
    writer.rule(Head_t::Choice, head, body);
    exp << (int) Rule_t::Choice << " 2 2 3 2 1 5 4\n";
    writer.external(3, Value_t::False);
    writer.external(4, Value_t::False);
    writer.minimize(0, min);
    writer.heuristic(1, Heuristic_t::Sign, 1, 0, {});
    exp << "1 6 0 0\n";
    exp << (int) Rule_t::Optimize << " 0 2 1 2 3 2 1\n";
    exp << (int) Rule_t::ClaspAssignExt << " 4 0\n";
    exp << (int) Rule_t::ClaspAssignExt << " 5 0\n";
    exp << "0\n6 _heuristic(_atom(2),sign,1,0)\n2 _atom(2)\n0\nB+\n0\nB-\n1\n0\n1\n";
    writer.endStep();
    writer.beginStep();
    exp << "90 0\n";
    head[0] = 3;
    head[1] = 4;
    writer.rule(Head_t::Choice, head, {});
    exp << (int) Rule_t::Choice << " 2 4 5 0 0\n";
    writer.endStep();
    exp << "0\n0\nB+\n0\nB-\n1\n0\n1\n";
    REQUIRE(str.str() == exp.str());
}

TEST_CASE("Convert to smodels", "[convert]") {
    using BodyLits = std::initializer_list<Lit_t>;
    using AggLits  = std::initializer_list<WeightLit_t>;
    ReadObserver   observer;
    SmodelsConvert convert(observer, true);
    convert.initProgram(false);
    convert.beginStep();
    SECTION("convert rule") {
        Atom_t   a    = 1;
        BodyLits lits = {4, -3, -2, 5};
        convert.rule(Head_t::Disjunctive, {&a, 1}, {begin(lits), lits.size()});
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        RawRule r = {convert.get(lit(a)), convert.get(4), convert.get(-3), convert.get(-2), convert.get(5)};
        REQUIRE(observer.rules[Rule_t::Basic][0] == r);
    }
    SECTION("convert sum rule") {
        Atom_t  a    = 1;
        AggLits lits = {{4, 2}, {-3, 3}, {-2, 1}, {5, 4}};
        convert.rule(Head_t::Disjunctive, {&a, 1}, 3, {begin(lits), lits.size()});
        REQUIRE(observer.rules[Rule_t::Weight].size() == 1);
        RawRule sr = {convert.get(lit(a)), 3, convert.get(4), 2, convert.get(-3), 3,
                      convert.get(-2),     1, convert.get(5), 4};
        REQUIRE(observer.rules[Rule_t::Weight][0] == sr);
    }
    SECTION("convert mixed rule") {
        std::initializer_list<Atom_t> h    = {1, 2, 3};
        AggLits                       lits = {{4, 2}, {-3, 3}, {-2, 1}, {5, 4}};
        convert.rule(Head_t::Choice, {begin(h), h.size()}, 3, {begin(lits), lits.size()});
        REQUIRE(observer.rules[Rule_t::Choice].size() == 1);
        REQUIRE(observer.rules[Rule_t::Weight].size() == 1);
        int     aux = (int) convert.maxAtom();
        RawRule cr  = {3, convert.get(1), convert.get(2), convert.get(3), aux};
        RawRule sr  = {aux, 3, convert.get(4), 2, convert.get(-3), 3, convert.get(-2), 1, convert.get(5), 4};
        REQUIRE(cr == observer.rules[Rule_t::Choice][0]);
        REQUIRE(sr == observer.rules[Rule_t::Weight][0]);
    }
    SECTION("convert satisfied sum rule") {
        Atom_t  a    = 1;
        AggLits lits = {{4, 2}, {-3, 3}, {-2, 1}, {5, 4}};
        convert.rule(Head_t::Disjunctive, {&a, 1}, -3, {begin(lits), lits.size()});
        REQUIRE(observer.rules[Rule_t::Weight].empty());
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        RawRule sr = {convert.get(lit(a))};
        REQUIRE(observer.rules[Rule_t::Basic][0] == sr);
    }
    SECTION("convert invalid rule") {
        std::initializer_list<Atom_t> h       = {1, 2, 3};
        AggLits                       invalid = {{4, 2}, {-3, -3}};
        REQUIRE_THROWS_AS(convert.rule(Head_t::Choice, {begin(h), h.size()}, 2, {begin(invalid), invalid.size()}),
                          std::logic_error);
    }

    SECTION("convert minimize") {
        AggLits m1 = {{4, 1}, {-3, -2}, {-2, 1}, {5, -1}};
        AggLits m2 = {{8, 1}, {-7, 2}, {-6, 1}, {9, 1}};
        convert.minimize(3, {begin(m2), 2});
        convert.minimize(10, {begin(m1), m1.size()});
        convert.minimize(3, {begin(m2) + 2, 2});
        REQUIRE(observer.rules[Rule_t::Optimize].empty());
        convert.endStep();
        REQUIRE(observer.rules[Rule_t::Optimize].size() == 2);

        RawRule m3  = {3, convert.get(8), 1, convert.get(-7), 2, convert.get(-6), 1, convert.get(9), 1};
        RawRule m10 = {10, convert.get(4), 1, convert.get(3), 2, convert.get(-2), 1, convert.get(-5), 1};
        REQUIRE(observer.rules[Rule_t::Optimize][0] == m3);
        REQUIRE(observer.rules[Rule_t::Optimize][1] == m10);
    }
    SECTION("convert output") {
        LitVec c = {1, -2, 3};
        convert.output({"Foo", 3}, c);
        convert.endStep();
        REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
        REQUIRE(convert.maxAtom() == 5);
        auto aux = observer.rules[Rule_t::Basic][0][0];
        REQUIRE(observer.atoms.at(aux) == "Foo");
    }

    SECTION("convert external") {
        convert.external(1, Value_t::Free);
        convert.external(2, Value_t::True);
        convert.external(3, Value_t::False);
        convert.external(4, Value_t::Release);
        convert.endStep();
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt].size() == 3);
        REQUIRE(observer.rules[Rule_t::ClaspReleaseExt].size() == 1);
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][0][1] == int(Value_t::Free));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][1][1] == int(Value_t::True));
        REQUIRE(observer.rules[Rule_t::ClaspAssignExt][2][1] == int(Value_t::False));
    }
    SECTION("edges are converted to atoms") {
        Lit_t a = 1;
        convert.output({"a", 1}, {&a, 1});
        convert.acycEdge(0, 1, {&a, 1});
        LitVec c = {1, 2, 3};
        convert.acycEdge(1, 0, c);
        convert.endStep();
        REQUIRE(observer.rules[Rule_t::Basic].size() == 2);
        REQUIRE(observer.atoms[observer.rules[Rule_t::Basic][0][0]] == "_edge(0,1)");
        REQUIRE(observer.atoms[observer.rules[Rule_t::Basic][1][0]] == "_edge(1,0)");
    }

    SECTION("heuristics are converted to atoms") {
        SECTION("empty condition") {
            Lit_t a = 1;
            convert.heuristic(static_cast<Atom_t>(a), Heuristic_t::Factor, 10, 2, {});
            convert.output({"a", 1}, {&a, 1});
            convert.endStep();
            REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
            REQUIRE(convert.maxAtom() == 3);
            REQUIRE(observer.atoms[observer.rules[Rule_t::Basic][0][0]] == "_heuristic(a,factor,10,2)");
        }
        SECTION("named condition requires aux atom while unnamed does not") {
            Lit_t a = 1, b = 2, c = 3;
            convert.output({"a", 1}, {&a, 1});
            convert.output({"b", 1}, {&b, 1});
            convert.heuristic(static_cast<Atom_t>(a), Heuristic_t::Level, 10, 2, {&b, 1});
            convert.heuristic(static_cast<Atom_t>(a), Heuristic_t::Init, 10, 2, {&c, 1});
            convert.endStep();
            REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
            REQUIRE(observer.atoms[observer.rules[Rule_t::Basic][0][0]] == "_heuristic(a,level,10,2)");
            REQUIRE(observer.atoms[convert.get(c)] == "_heuristic(a,init,10,2)");
        }
        SECTION("unused atom is ignored") {
            Lit_t a = 1;
            convert.heuristic(static_cast<Atom_t>(a), Heuristic_t::Sign, -1, 2, {});
            convert.endStep();
            REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
            REQUIRE(convert.maxAtom() == 2);
            REQUIRE(observer.atoms.empty());
        }
        SECTION("unnamed atom requires aux name") {
            Atom_t a = 1;
            convert.rule(Head_t::Choice, {&a, 1}, {});
            convert.heuristic(a, Heuristic_t::Sign, -1, 2, {});
            convert.endStep();
            REQUIRE(observer.rules[Rule_t::Basic].size() == 1);
            REQUIRE(convert.maxAtom() == 3);
            REQUIRE(observer.atoms[2] == "_atom(2)");
            REQUIRE(observer.atoms[3] == "_heuristic(_atom(2),sign,-1,2)");
        }
    }
}

TEST_CASE("Test Atom to directive conversion", "[clasp]") {
    ReadObserver          observer;
    std::stringstream     str;
    SmodelsOutput         writer(str, false, 0);
    SmodelsInput::Options opts;
    opts.enableClaspExt().convertEdges().convertHeuristic();
    std::vector<Atom_t> atoms = {1, 2, 3, 4, 5, 6};
    writer.initProgram(false);
    writer.beginStep();
    writer.rule(Potassco::Head_t::Choice, atoms, {});
    SECTION("_edge(X,Y) atoms are converted to edges directives") {
        Lit_t a = 1, b = 2, c = 3;
        writer.output("_edge(1,2)", {&a, 1});
        writer.output(R"(_edge("1,2","2,1"))", {&b, 1});
        writer.output(R"(_edge("2,1","1,2"))", {&c, 1});
        writer.endStep();
        REQUIRE(readSmodels(str, observer, opts) == 0);
        REQUIRE(observer.edges.size() == 3);
        REQUIRE(observer.edges[0].cond == Vec<Lit_t>({a}));
        REQUIRE(observer.edges[0].s == 0);
        REQUIRE(observer.edges[0].t == 1);
        REQUIRE(observer.edges[1].cond == Vec<Lit_t>({b}));
        REQUIRE(observer.edges[1].s == observer.edges[2].t);
        REQUIRE(observer.edges[2].cond == Vec<Lit_t>({c}));
    }
    SECTION("Test acyc") {
        Lit_t a = 1, b = 2;
        SECTION("_acyc_ atoms are converted to edge directives") {
            writer.output("_acyc_1_1234_4321", {&a, 1});
            writer.output("_acyc_1_4321_1234", {&b, 1});
        }
        SECTION("_acyc_ and _edge atoms can be mixed") {
            writer.output("_acyc_1_1234_4321", {&a, 1});
            writer.output("_edge(4321,1234)", {&b, 1});
        }
        writer.endStep();
        REQUIRE(readSmodels(str, observer, opts) == 0);
        REQUIRE(observer.edges.size() == 2);
        REQUIRE(observer.edges[0].cond == Vec<Lit_t>({a}));
        REQUIRE(observer.edges[1].cond == Vec<Lit_t>({b}));
        REQUIRE(observer.edges[0].s == observer.edges[1].t);
        REQUIRE(observer.edges[0].t == observer.edges[1].s);
    }
    SECTION("_heuristic atoms are converted to heuristic directive") {
        Lit_t a = 1, b = 2, h1 = 3, h2 = 4, h3 = 5, h4 = 6;
        writer.output("f(a,b,c,d(q(r(s))))", {&a, 1});
        writer.output("_heuristic(f(a,b,c,d(q(r(s)))),sign,-1)", {&h1, 1});
        writer.output("_heuristic(f(a,b,c,d(q(r(s)))),true,1)", {&h2, 1});
        writer.output("_heuristic(f(\"a,b(c,d)\"),level,-1,10)", {&h3, 1});
        writer.output("_heuristic(f(\"a,b(c,d)\"),factor,2,1)", {&h4, 1});
        writer.output("f(\"a,b(c,d)\")", {&b, 1});
        writer.endStep();
        SECTION("via default lookup") { REQUIRE(readSmodels(str, observer, opts) == 0); }
        SECTION("via user lookup") {
            SmodelsInput reader(observer, opts, [&](std::string_view name) {
                for (const auto& [id, n] : observer.atoms) {
                    if (n == name)
                        return static_cast<Atom_t>(id);
                }
                return Atom_t(0);
            });
            readProgram(str, reader);
        }

        REQUIRE(observer.heuristics.size() == 4);
        Heuristic exp[] = {{static_cast<Atom_t>(a), Heuristic_t::Sign, -1, 1, {h1}},
                           {static_cast<Atom_t>(a), Heuristic_t::True, 1, 1, {h2}},
                           {static_cast<Atom_t>(b), Heuristic_t::Level, -1, 10, {h3}},
                           {static_cast<Atom_t>(b), Heuristic_t::Factor, 2, 1, {h4}}};
        REQUIRE(std::equal(std::begin(exp), std::end(exp), observer.heuristics.begin()) == true);
    }
}

} // namespace Potassco::Test::Smodels
