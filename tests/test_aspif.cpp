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
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
#include "test_common.h"

#include <potassco/aspif.h>
#include <potassco/aspif_text.h>
#include <potassco/rule_utils.h>
#include <potassco/theory_data.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>

namespace Potassco::Test::Aspif {
const Weight_t BOUND_NONE = -1;
static void    finalize(std::stringstream& str) { str << "0\n"; }
static void    rule(std::ostream& os, const Rule& r) {
    os << (unsigned) Directive_t::Rule << " " << (unsigned) r.ht << " ";
    os << r.head.size();
    for (auto x : r.head) { os << " " << x; }
    os << " " << (unsigned) r.bt << " ";
    if (r.bt == Body_t::Sum) {
        os << r.bnd << " " << r.body.size();
        std::for_each(begin(r.body), end(r.body), [&os](WeightLit_t x) { os << " " << x.lit << " " << x.weight; });
    }
    else {
        os << r.body.size();
        std::for_each(begin(r.body), end(r.body), [&os](WeightLit_t x) { os << " " << x.lit; });
    }
    os << "\n";
}
static std::ostream& operator<<(std::ostream& os, const WeightLit_t& wl) {
    return os << "(" << wl.lit << "," << wl.weight << ")";
}
static std::ostream& operator<<(std::ostream& os, const std::pair<Atom_t, Potassco::Value_t>& wl) {
    return os << "(" << wl.first << "," << static_cast<unsigned>(wl.second) << ")";
}
static std::ostream& operator<<(std::ostream& os, const Heuristic& h);
static std::ostream& operator<<(std::ostream& os, const Edge& e);
template <typename T>
std::string stringify(const std::span<T>& s) {
    std::stringstream str;
    str << "[";
    const char* sep = "";
    for (const auto& e : s) { str << std::exchange(sep, ", ") << e; }
    str << "]";
    return str.str();
}
static std::ostream& operator<<(std::ostream& os, const Heuristic& h) {
    return os << "h(" << h.atom << "," << static_cast<unsigned>(h.type) << "," << h.bias << "," << h.prio << ") "
              << stringify(std::span{h.cond}) << ".";
}
static std::ostream& operator<<(std::ostream& os, const Edge& e) {
    return os << "e(" << e.s << "," << e.t << ") " << stringify(std::span{e.cond}) << ".";
}
class ReadObserver : public Test::ReadObserver {
public:
    void rule(Head_t ht, const AtomSpan& head, const LitSpan& body) override {
        rules.push_back({ht, {begin(head), end(head)}, Body_t::Normal, BOUND_NONE, {}});
        Vec<WeightLit_t>& wb = rules.back().body;
        std::for_each(begin(body), end(body), [&wb](Lit_t x) { wb.push_back({x, 1}); });
    }
    void rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) override {
        rules.push_back({ht, {begin(head), end(head)}, Body_t::Sum, bound, {begin(body), end(body)}});
    }
    void minimize(Weight_t prio, const WeightLitSpan& lits) override {
        min.push_back({prio, {begin(lits), end(lits)}});
    }
    void project(const AtomSpan& atoms) override { projects.insert(projects.end(), begin(atoms), end(atoms)); }
    void output(const std::string_view& str, const LitSpan& cond) override {
        shows.push_back({{begin(str), end(str)}, {begin(cond), end(cond)}});
    }

    void external(Atom_t a, Value_t v) override { externals.emplace_back(a, v); }
    void assume(const LitSpan& lits) override { assumes.insert(assumes.end(), begin(lits), end(lits)); }
    void theoryTerm(Id_t termId, int number) override { theory.addTerm(termId, number); }
    void theoryTerm(Id_t termId, const std::string_view& name) override { theory.addTerm(termId, name); }
    void theoryTerm(Id_t termId, int cId, const IdSpan& args) override {
        theory.addTerm(termId, static_cast<Id_t>(cId), args);
    }
    void theoryElement(Id_t elementId, const IdSpan& terms, const LitSpan&) override {
        theory.addElement(elementId, terms, 0u);
    }
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) override {
        theory.addAtom(atomOrZero, termId, elements);
    }
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) override {
        theory.addAtom(atomOrZero, termId, elements, op, rhs);
    }

    Vec<Rule>                               rules;
    Vec<std::pair<int, Vec<WeightLit_t>>>   min;
    Vec<std::pair<std::string, Vec<Lit_t>>> shows;
    Vec<std::pair<Atom_t, Value_t>>         externals;
    Vec<Atom_t>                             projects;
    Vec<Lit_t>                              assumes;
    TheoryData                              theory;
};

static unsigned compareRead(std::stringstream& input, ReadObserver& observer, const Rule* rules,
                            const std::pair<unsigned, unsigned>& subset) {
    for (unsigned i = 0; i != subset.second; ++i) { rule(input, rules[subset.first + i]); }
    finalize(input);
    readAspif(input, observer);
    if (observer.rules.size() != subset.second) {
        return static_cast<unsigned>(observer.rules.size());
    }
    for (unsigned i = 0; i != subset.second; ++i) {
        if (!(rules[subset.first + i] == observer.rules[i])) {
            return i;
        }
    }
    return subset.second;
}
TEST_CASE("Test DynamicBuffer", "[rule]") {
    SECTION("starts empty") {
        DynamicBuffer r;
        REQUIRE(r.size() == 0);
        REQUIRE(r.capacity() == 0);
        REQUIRE(r.data() == nullptr);
    }
    SECTION("supports initial size") {
        DynamicBuffer r(256);
        REQUIRE(r.capacity() == 256);
        REQUIRE(r.size() == 0);
        REQUIRE(r.data() != nullptr);
    }
    SECTION("grows geometrically") {
        DynamicBuffer r;
        auto          fillAvail = [](DynamicBuffer& reg, char f) {
            auto a = reg.capacity() - reg.size();
            auto u = reg.alloc(a);
            std::memset(u.data(), f, u.size());
            return u.size();
        };
        std::string exp;
        char        c = 'a';
        r.push(c);
        REQUIRE(r.capacity() == 64);
        fillAvail(r, c);
        CHECK(r.size() == 64);
        CHECK(r.capacity() == 64);
        exp.append(64, c);
        for (++c; r.capacity() <= 0x20000u; ++c) {
            auto old = r.capacity();
            r.push(c);
            REQUIRE(r.capacity() >= (old * 1.5));
            exp.append(fillAvail(r, c) + 1, c);
        }
        CHECK(exp == r.view());
        auto old = r.capacity();
        r.push(c);
        REQUIRE(r.capacity() >= (old * 2.0));
    }
    SECTION("copies data on realloc") {
        DynamicBuffer r;
        std::string   exp;
        std::memset(r.alloc(12).data(), 'A', 12);
        exp.append(12, 'A');
        std::memset(r.alloc(13).data(), 'B', 13);
        exp.append(13, 'B');
        std::memset(r.alloc(14).data(), 'C', 14);
        exp.append(14, 'C');
        r.push(0);
        (void) r.alloc((r.capacity() - r.size()) + 1);
        REQUIRE(exp == r.data());
    }
    SECTION("copy and move") {
        static_assert(std::is_move_constructible_v<DynamicBuffer>, "should be movable");
        static_assert(std::is_move_assignable_v<DynamicBuffer>, "should be movable");
        static_assert(std::is_copy_assignable_v<DynamicBuffer>, "should not be copyable");
        static_assert(std::is_copy_constructible_v<DynamicBuffer>, "should not be copyable");
        DynamicBuffer m1;
        std::string   exp(50, 'x');
        m1.append(exp.data(), exp.size());
        m1.push(0);
        auto  sz  = m1.size();
        auto  cp  = m1.capacity();
        auto* beg = m1.data();
        CHECK(exp == beg);

        SECTION("move construct") {
            DynamicBuffer m2(std::move(m1));
            CHECK(m1.capacity() == 0);
            CHECK(m1.data() == nullptr);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() == cp);
            CHECK(beg == m2.data());
            CHECK(exp == beg);
        }

        SECTION("copy construct") {
            DynamicBuffer m2(m1);
            CHECK(m2.size() == sz);
            CHECK(m1.size() == sz);
            CHECK(m1.capacity() == cp);
            CHECK(m2.capacity() <= cp);
            CHECK(m1.data() == beg);
            CHECK(m2.data() != beg);
            CHECK(exp == m2.data());
        }

        SECTION("move assign") {
            DynamicBuffer m2;
            memset(m2.alloc(100).data(), 'y', 100);
            m2 = std::move(m1);
            CHECK(m1.size() == 0);
            CHECK(m1.data() == nullptr);
            CHECK(m2.data() == beg);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() == cp);
            CHECK(exp == beg);
        }

        SECTION("copy assign") {
            DynamicBuffer m2;
            memset(m2.alloc(100).data(), 'y', 100);
            m2 = m1;
            CHECK(m1.size() == sz);
            CHECK(m1.data() == beg);
            CHECK(m2.data() != beg);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() <= cp);
            CHECK(exp == m2.data());
        }
    }
}
TEST_CASE("Test FixedString", "[rule]") {
    SECTION("empty") {
        static_assert(sizeof(FixedString) == 24);
        FixedString s;
        REQUIRE(s.size() == 0);
        REQUIRE(s.c_str() != nullptr);
        REQUIRE_FALSE(*s.c_str());
    }
    SECTION("small to large") {
        std::string v;
        for (unsigned i = 0;; ++i) {
            v.assign(i, 'x');
            FixedString s(v);
            REQUIRE(v.size() == i);
            REQUIRE(s.size() == v.size());
            REQUIRE(std::strcmp(s.c_str(), v.c_str()) == 0);
            REQUIRE(static_cast<std::string_view>(s) == v);
            if (not s.small())
                break;
        }
        REQUIRE(v.size() == 24);
    }
    SECTION("deep copy") {
        std::string_view sv("small");
        FixedString      s(sv);
        FixedString      s2(s);
        REQUIRE(s == sv);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s2 == sv);
        REQUIRE((void*) s.c_str() != (void*) s2.c_str());
        std::string large(32, 'x');
        FixedString s3(large);
        FixedString s4(s3);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s3 == std::string_view{large});
        REQUIRE(s4 == std::string_view{large});
        REQUIRE((void*) s3.c_str() != (void*) s4.c_str());
    }
    SECTION("shallow copy") {
        std::string_view sv("small");
        FixedString      s(sv, FixedString::Shared);
        FixedString      s2(s);
        REQUIRE(s == sv);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s2 == sv);
        REQUIRE((void*) s.c_str() != (void*) s2.c_str());
        std::string large(32, 'x');
        FixedString s3(large, FixedString::Shared);
        FixedString s4(s3);
        REQUIRE(s3.shareable());
        REQUIRE(s4.shareable());
        REQUIRE(s3 == std::string_view{large});
        REQUIRE(s4 == std::string_view{large});
        REQUIRE((void*) s3.c_str() == (void*) s4.c_str());
    }
    SECTION("move") {
        std::string_view sv("small");
        FixedString      s(sv);
        FixedString      s2(std::move(s));
        REQUIRE(s == std::string_view{});
        REQUIRE(s2 == sv);
        std::string large(32, 'x');
        FixedString s3(large);
        auto        old = (void*) s3.c_str();
        FixedString s4(std::move(s3));
        REQUIRE(s3 == std::string_view{});
        REQUIRE(s4 == std::string_view(large));
        REQUIRE((void*) s4.c_str() == old);
    }
    SECTION("compare") {
        FixedString s("one");
        FixedString t("two");
        REQUIRE(s == s);
        REQUIRE(s != t);
        REQUIRE(s < t);
        REQUIRE(t > s);
        REQUIRE_FALSE(s < s);
        REQUIRE_FALSE(s > s);
        REQUIRE(s <= s);
        REQUIRE(s >= s);

        auto sv = s.view();
        auto tv = t.view();
        REQUIRE(s == sv);
        REQUIRE(s != tv);
        REQUIRE(s < tv);
        REQUIRE(tv > s);
        REQUIRE_FALSE(s < sv);
        REQUIRE_FALSE(sv > s);
        REQUIRE(sv <= s);
        REQUIRE(s >= sv);
    }
}
template <typename T, typename U>
static bool spanEq(const T& lhs, const U& rhs) {
    if (std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end())) {
        return true;
    }
    UNSCOPED_INFO("LHS: " << stringify(std::span(lhs)) << "\n  RHS: " << stringify(std::span(rhs)));
    return false;
}

template <typename T>
static bool spanEq(const std::span<const T>& lhs, T* f, T* e) {
    if (lhs.data() != f) {
        UNSCOPED_INFO("Mismatch: begin does not match span begin");
        return false;
    }
    if (lhs.data() + lhs.size() != e) {
        UNSCOPED_INFO("Mismatch: end does not match span end");
        return false;
    }
    return true;
}

TEST_CASE("Test Basic", "[rule]") {
    SECTION("atom") {
        static_assert(std::is_same_v<Atom_t, uint32_t>);
        static_assert(atomMin > 0);
        static_assert(atomMin < UINT32_MAX);
        auto x = Atom_t(7);
        CHECK(weight(x) == 1);
        CHECK(id(x) == x);
    }
    SECTION("literal") {
        static_assert(std::is_same_v<Lit_t, int32_t>);
        auto x = Lit_t(7);
        CHECK(weight(x) == 1);
        CHECK(atom(x) == 7);
        CHECK(neg(x) == -7);
        CHECK(id(x) == 7);
        CHECK(lit(id(neg(x))) == -7);
    }
    SECTION("weight literal") {
        WeightLit_t wl{.lit = -4, .weight = 3};
        CHECK(weight(wl) == 3);
        CHECK(lit(wl) == -4);
        CHECK(wl == wl);
        CHECK_FALSE(wl != wl);
        CHECK_FALSE(wl < wl);
        CHECK_FALSE(wl > wl);
        CHECK(wl <= wl);
        CHECK(wl >= wl);
        CHECK(wl < lit(3));
        CHECK(lit(3) > wl);
        CHECK(lit(-4) != wl);
        CHECK(lit(-4) < wl);
        CHECK(wl > lit(-4));
    }
}
TEST_CASE("Test RuleBuilder", "[rule]") {
    RuleBuilder rb;

    SECTION("simple rule") {
        rb.start().addHead(1).addGoal(2).addGoal(-3).end();
        REQUIRE(spanEq(rb.head(), std::vector{1}));
        REQUIRE(rb.bodyType() == Body_t::Normal);
        REQUIRE(spanEq(rb.body(), std::vector{2, -3}));
        REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("simple constraint") {
        SECTION("head first") { rb.start().addGoal(2).addGoal(-3).end(); }
        SECTION("body first") { rb.startBody().addGoal(2).addGoal(-3).start().end(); }
        CHECK(spanEq(rb.head(), std::vector<Atom_t>{}));
        CHECK(rb.bodyType() == Body_t::Normal);
        CHECK(spanEq(rb.body(), std::vector{2, -3}));
        CHECK(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("simple choice") {
        SECTION("head first") { rb.start(Head_t::Choice).addHead(1).addHead(2).startBody().end(); }
        SECTION("body first") { rb.startBody().start(Head_t::Choice).addHead(1).addHead(2).end(); }
        CHECK(spanEq(rb.head(), std::vector<Atom_t>{1, 2}));
        CHECK(rb.bodyType() == Body_t::Normal);
        CHECK(spanEq(rb.body(), std::vector<Lit_t>{}));
        CHECK(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("simple weight rule") {
        rb.start().addHead(1).startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).end();
        REQUIRE(spanEq(rb.head(), std::vector{1}));
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(rb.bound() == 2);
        REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{2, 1}, {-3, 1}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));
    }
    SECTION("update bound") {
        rb.start().addHead(1).startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).setBound(3);
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(rb.bound() == 3);
        rb.clear();
        rb.startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).addHead(1).setBound(4);
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(rb.bound() == 4);
        rb.clear();
        rb.startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).addHead(1).end();
        REQUIRE_THROWS_AS(rb.setBound(4), std::logic_error);
    }
    SECTION("weakean to cardinality rule") {
        rb.start().addHead(1).startSum(2).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).weaken(Body_t::Count).end();
        REQUIRE(spanEq(rb.head(), std::vector{1}));
        REQUIRE(rb.bodyType() == Body_t::Count);
        REQUIRE(rb.bound() == 1);
        REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{2, 1}, {-3, 1}, {4, 1}}));
        REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));
    }
    SECTION("weaken to normal rule") {
        rb.start().addHead(1).startSum(3).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).weaken(Body_t::Normal).end();
        REQUIRE(spanEq(rb.head(), std::vector{1}));
        REQUIRE(rb.bodyType() == Body_t::Normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{2, -3, 4}));
        REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("weak to normal rule - inverse order") {
        rb.startSum(3).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).start().addHead(1).weaken(Body_t::Normal).end();
        REQUIRE(spanEq(rb.head(), std::vector{1}));
        REQUIRE(rb.bodyType() == Body_t::Normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{2, -3, 4}));
        REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("minimize rule") {
        SECTION("implicit body") {
            rb.startMinimize(1).addGoal(-3, 2).addGoal(4, 1).addGoal(5).end();
            REQUIRE(spanEq(rb.head(), std::vector<Lit_t>{}));
            REQUIRE(rb.isMinimize());
            REQUIRE(rb.bodyType() == Body_t::Sum);
            REQUIRE(rb.bound() == 1);
            REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{-3, 2}, {4, 1}, {5, 1}}));
            REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));
        }
        SECTION("explicit body") {
            rb.startMinimize(1).startSum(0).addGoal(-3, 2).addGoal(4, 1).addGoal(5).end();
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{}));
            REQUIRE(rb.isMinimize());
            REQUIRE(rb.bodyType() == Body_t::Sum);
            REQUIRE(rb.bound() == 1);
            REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{-3, 2}, {4, 1}, {5, 1}}));
            REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));
        }
    }
    SECTION("clear body") {
        rb.startSum(3)
            .addGoal(2, 2)
            .addGoal(-3, 2)
            .addGoal(4, 2)
            .start()
            .addHead(1)
            .clearBody()
            .startBody()
            .addGoal(5)
            .end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == Body_t::Normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{5}));
        REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));

        rb.start()
            .addHead(1)
            .startSum(3)
            .addGoal(2, 2)
            .addGoal(-3, 2)
            .addGoal(4, 2)
            .clearBody()
            .startBody()
            .addGoal(5)
            .end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == Body_t::Normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{5}));
        REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
    }
    SECTION("clear head") {
        rb.startSum(3)
            .addGoal(2, 2)
            .addGoal(-3, 2)
            .addGoal(4, 2)
            .start()
            .addHead(1)
            .clearHead()
            .start()
            .addHead(5)
            .end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{5}));
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{2, 2}, {-3, 2}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));

        rb.start()
            .addHead(1)
            .startSum(3)
            .addGoal(2, 2)
            .addGoal(-3, 2)
            .addGoal(4, 2)
            .clearHead()
            .start()
            .addHead(5)
            .end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{5}));
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{2, 2}, {-3, 2}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));
    }

    SECTION("copy and move") {
        rb.start().addHead(1).startSum(25);
        std::vector<WeightLit_t> exp;
        for (int i = 2; i != 20; ++i) {
            auto wl = WeightLit_t{(i & 1) ? i : -i, i};
            rb.addGoal(wl);
            exp.push_back(wl);
        }
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.sum().bound == 25);
        REQUIRE(spanEq(rb.sum().lits, exp));
        REQUIRE(spanEq(rb.sum().lits, rb.wlits_begin(), rb.wlits_end()));

        SECTION("copy") {
            RuleBuilder copy(rb);
            REQUIRE(spanEq(copy.head(), std::vector<Atom_t>{1}));
            REQUIRE(copy.sum().bound == 25);
            REQUIRE(spanEq(copy.sum().lits, exp));
            REQUIRE(spanEq(copy.sum().lits, copy.wlits_begin(), copy.wlits_end()));

            auto newLit = WeightLit_t{4711, 31};
            copy.addGoal(newLit);
            REQUIRE(spanEq(rb.sum().lits, exp));

            exp.push_back(newLit);
            REQUIRE(spanEq(copy.sum().lits, exp));
            REQUIRE(spanEq(copy.sum().lits, copy.wlits_begin(), copy.wlits_end()));
        }
        SECTION("move") {
            RuleBuilder mv(std::move(rb));
            REQUIRE(spanEq(mv.head(), std::vector<Atom_t>{1}));
            REQUIRE(mv.sum().bound == 25);
            REQUIRE(spanEq(mv.sum().lits, exp));
            REQUIRE(spanEq(mv.sum().lits, mv.wlits_begin(), mv.wlits_end()));

            REQUIRE(rb.head().size() == 0);
            REQUIRE(rb.body().size() == 0);

            rb.start().addHead(1).addGoal(2).addGoal(-3).end();
            REQUIRE(spanEq(rb.head(), std::vector{1}));
            REQUIRE(rb.bodyType() == Body_t::Normal);
            REQUIRE(spanEq(rb.body(), std::vector{2, -3}));
            REQUIRE(spanEq(rb.body(), rb.lits_begin(), rb.lits_end()));
        }
    }

    SECTION("freeze unfreeze") {
        SECTION("start twice is invalid") {
            REQUIRE_THROWS_AS(RuleBuilder().addHead(1).start(), std::logic_error);
            REQUIRE_THROWS_AS(RuleBuilder().startBody().addGoal(2).startBody(), std::logic_error);
            REQUIRE_THROWS_AS(RuleBuilder().startBody().addGoal(2).addHead(1).startBody(), std::logic_error);
            REQUIRE_THROWS_AS(RuleBuilder().start().addHead(1).addGoal(2).start(), std::logic_error);
        }
        SECTION("add after other start is invalid") {
            REQUIRE_THROWS_AS(RuleBuilder().start().addHead(1).addGoal(2).addHead(3), std::logic_error);
            REQUIRE_THROWS_AS(RuleBuilder().startBody().addGoal(2).addHead(1).addGoal(3), std::logic_error);
        }
        SECTION("add after other start is invalid") {
            REQUIRE_THROWS_AS(RuleBuilder().start().addHead(1).addGoal(2).addHead(3), std::logic_error);
            REQUIRE_THROWS_AS(RuleBuilder().startBody().addGoal(2).addHead(1).addGoal(3), std::logic_error);
        }
        SECTION("start after end clears") {
            REQUIRE_NOTHROW(rb.start().addHead(1).addGoal(2).end().start().addHead(3));
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{3}));
            REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{}));
            rb.clear();

            REQUIRE_NOTHROW(rb.startBody().addGoal(2).addHead(1).end().startBody().addGoal(3));
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{}));
            REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{3}));
        }
    }
    SECTION("grow bug") {
        for (int i = 0; i != 12; ++i) { rb.addHead(static_cast<Atom_t>(i + 1)); }
        rb.startSum(22).addGoal(47, 11).addGoal(18, 15).addGoal(17, 7).end();
        REQUIRE(rb.bodyType() == Body_t::Sum);
        REQUIRE(rb.bound() == 22);
        REQUIRE(spanEq(rb.sum().lits, std::vector<WeightLit_t>{{47, 11}, {18, 15}, {17, 7}}));
    }
}
TEST_CASE("Intermediate Format Reader ", "[aspif]") {
    std::stringstream input;
    ReadObserver      observer;
    input << "asp 1 0 0\n";
    SECTION("read empty") {
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.nStep == 1);
        REQUIRE(observer.incremental == false);
    }
    SECTION("read empty rule") {
        rule(input, {Head_t::Disjunctive, {}, Body_t::Normal, BOUND_NONE, {}});
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.rules.size() == 1);
        REQUIRE(observer.rules[0].head.empty());
        REQUIRE(observer.rules[0].body.empty());
    }
    SECTION("read rules") {
        Rule rules[] = {{Head_t::Disjunctive, {1}, Body_t::Normal, BOUND_NONE, {{-2, 1}, {3, 1}, {-4, 1}}},
                        {Head_t::Disjunctive, {1, 2, 3}, Body_t::Normal, BOUND_NONE, {{5, 1}, {-6, 1}}},
                        {Head_t::Disjunctive, {}, Body_t::Normal, BOUND_NONE, {{1, 1}, {2, 1}}},
                        {Head_t::Choice, {1, 2, 3}, Body_t::Normal, BOUND_NONE, {{5, 1}, {-6, 1}}},
                        // weight
                        {Head_t::Disjunctive, {1}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        {Head_t::Disjunctive, {2}, Body_t::Sum, 1, {{3, 1}, {-4, 1}, {5, 1}}},
                        // mixed
                        {Head_t::Choice, {1, 2}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        {Head_t::Disjunctive, {}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        // negative weights
                        {Head_t::Disjunctive, {1}, Body_t::Sum, 1, {{2, 1}, {-3, -2}, {-4, 3}, {5, 1}}}};
        using Pair   = std::pair<unsigned, unsigned>;
        Pair basic(0, 4);
        Pair weight(4, 2);
        Pair mixed(6, 2);
        Pair neg(8, 1);
        SECTION("simple rules with normal bodies") {
            REQUIRE(compareRead(input, observer, rules, basic) == basic.second);
        }
        SECTION("read rules with weight body") {
            REQUIRE(compareRead(input, observer, rules, weight) == weight.second);
        }
        SECTION("read mixed rules") { REQUIRE(compareRead(input, observer, rules, mixed) == mixed.second); }
        SECTION("negative weights not allowed in weight rule") {
            REQUIRE_THROWS(compareRead(input, observer, rules, neg));
        }
    }
    SECTION("read minimize rule") {
        input << (unsigned) Directive_t::Minimize << " -1 3 4 5 6 1 3 2\n";
        input << (unsigned) Directive_t::Minimize << " 10 3 4 -52 -6 36 3 -20\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.min.size() == 2);
        const auto& mr1 = observer.min[0];
        const auto& mr2 = observer.min[1];
        REQUIRE(mr1.first == -1);
        REQUIRE(mr2.first == 10);
        auto lits = Vec<WeightLit_t>{{4, 5}, {6, 1}, {3, 2}};
        REQUIRE(mr1.second == lits);
        lits = Vec<WeightLit_t>{{4, -52}, {-6, 36}, {3, -20}};
        REQUIRE(mr2.second == lits);
    }
    SECTION("read output") {
        input << (unsigned) Directive_t::Output << " 1 a 1 1\n";
        input << (unsigned) Directive_t::Output << " 10 Hallo Welt 2 1 -2\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.shows.size() == 2);
        const auto& s1 = observer.shows[0];
        const auto& s2 = observer.shows[1];
        REQUIRE(s1.first == "a");
        REQUIRE(s1.second == Vec<Lit_t>({1}));
        REQUIRE(s2.first == "Hallo Welt");
        REQUIRE(s2.second == Vec<Lit_t>({1, -2}));
    }
    SECTION("read projection") {
        input << (unsigned) Directive_t::Project << " 3 1 2 987232\n";
        input << (unsigned) Directive_t::Project << " 1 17\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.projects == Vec<Atom_t>({1, 2, 987232, 17}));
    }
    SECTION("read external") {
        std::pair<Atom_t, Value_t> exp[] = {
            {1, Value_t::Free}, {2, Value_t::True}, {3, Value_t::False}, {4, Value_t::Release}};
        for (auto&& e : exp) {
            input << (unsigned) Directive_t::External << " " << e.first << " " << (unsigned) e.second << "\n";
        }
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(spanEq(observer.externals, std::span{exp, std::size(exp)}));
    }
    SECTION("read assumptions") {
        input << (unsigned) Directive_t::Assume << " 2 1 987232\n";
        input << (unsigned) Directive_t::Assume << " 1 -2\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.assumes == Vec<Lit_t>({1, 987232, -2}));
    }
    SECTION("read edges") {
        input << (unsigned) Directive_t::Edge << " 0 1 2 1 -2\n";
        input << (unsigned) Directive_t::Edge << " 1 0 1 3\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.edges.size() == 2);
        REQUIRE(observer.edges[0].s == 0);
        REQUIRE(observer.edges[0].t == 1);
        REQUIRE(observer.edges[0].cond == Vec<Lit_t>({1, -2}));
        REQUIRE(observer.edges[1].s == 1);
        REQUIRE(observer.edges[1].t == 0);
        REQUIRE(observer.edges[1].cond == Vec<Lit_t>({3}));
    }
    SECTION("read heuristic") {
        Heuristic exp[] = {{1, Heuristic_t::Sign, -1, 1, {10}},
                           {2, Heuristic_t::Level, 10, 3, {-1, 10}},
                           {1, Heuristic_t::Init, 20, 1, {}},
                           {1, Heuristic_t::Factor, 2, 2, {}}};
        for (auto&& r : exp) {
            input << (unsigned) Directive_t::Heuristic << " " << (unsigned) r.type << " " << r.atom << " " << r.bias
                  << " " << r.prio << " " << r.cond.size();
            for (auto&& p : r.cond) { input << " " << p; }
            input << "\n";
        }
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(spanEq(observer.heuristics, std::span{exp, std::size(exp)}));
    }
    SECTION("read theory") {
        input << (unsigned) Directive_t::Theory << " 0 1 200\n"
              << (unsigned) Directive_t::Theory << " 0 6 1\n"
              << (unsigned) Directive_t::Theory << " 0 11 2\n"
              << (unsigned) Directive_t::Theory << " 1 0 4 diff\n"
              << (unsigned) Directive_t::Theory << " 1 2 2 <=\n"
              << (unsigned) Directive_t::Theory << " 1 4 1 -\n"
              << (unsigned) Directive_t::Theory << " 1 5 3 end\n"
              << (unsigned) Directive_t::Theory << " 1 8 5 start\n"
              << (unsigned) Directive_t::Theory << " 2 10 4 2 7 9\n"
              << (unsigned) Directive_t::Theory << " 2 7 5 1 6\n"
              << (unsigned) Directive_t::Theory << " 2 9 8 1 6\n"
              << (unsigned) Directive_t::Theory << " 4 0 1 10 0\n"
              << (unsigned) Directive_t::Theory << " 6 0 0 1 0 2 1\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.theory.numAtoms() == 1);

        class AtomVisitor : public TheoryData::Visitor {
        public:
            void visit(const TheoryData& data, Id_t, const TheoryTerm& t) override {
                if (auto type = t.type(); type == Theory_t::Number)
                    out << t.number();
                else if (type == Theory_t::Symbol)
                    out << t.symbol();
                else if (t.isFunction())
                    function(data, t);
            }
            void visit(const TheoryData& data, Id_t, const TheoryElement& e) override {
                out << '{';
                data.accept(e, *this, TheoryData::visit_all);
                out << '}';
            }
            void visit(const TheoryData& data, const TheoryAtom& a) override {
                out << '&';
                data.accept(a, *this, TheoryData::visit_all);
            }
            void function(const TheoryData& data, const TheoryTerm& t) {
                out << data.getTerm(t.function()).symbol() << '(';
                auto arg = 0;
                for (auto n : t.terms()) {
                    out << (arg++ > 0 ? "," : "");
                    AtomVisitor::visit(data, n, data.getTerm(n));
                }
                out << ')';
            }
            std::stringstream out;
        };
        AtomVisitor vis;
        observer.theory.accept(vis);
        REQUIRE(vis.out.str() == "&diff{-(end(1),start(1))}<=200");
    }
    SECTION("ignore comments") {
        input << (unsigned) Directive_t::Comment << "Hello World" << "\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
    }
}
TEST_CASE("Intermediate Format Reader supports incremental programs", "[aspif]") {
    std::stringstream input;
    ReadObserver      observer;
    input << "asp 1 0 0 incremental\n";
    SECTION("read empty steps") {
        finalize(input);
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.incremental == true);
        REQUIRE(observer.nStep == 2);
    }
    SECTION("read rules in each steps") {
        rule(input, {Head_t::Disjunctive, {1, 2}, Body_t::Normal, BOUND_NONE, {}});
        finalize(input);
        rule(input, {Head_t::Disjunctive, {3, 4}, Body_t::Normal, BOUND_NONE, {}});
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.incremental == true);
        REQUIRE(observer.nStep == 2);
        REQUIRE(observer.rules.size() == 2);
    }
}

TEST_CASE("Intermediate Format Reader requires current version", "[aspif]") {
    std::stringstream input;
    ReadObserver      observer;
    input << "asp 1 2 0 incremental\n";
    finalize(input);
    REQUIRE_THROWS(readAspif(input, observer));
}
TEST_CASE("Intermediate Format Reader requires incremental tag for incremental programs", "[aspif]") {
    std::stringstream input;
    ReadObserver      observer;
    input << "asp 1 0 0\n";
    finalize(input);
    finalize(input);
    REQUIRE_THROWS(readAspif(input, observer));
}

TEST_CASE("Test AspifOutput", "[aspif]") {
    std::stringstream out;
    AspifOutput       writer(out);
    ReadObserver      observer;
    writer.initProgram(false);
    writer.beginStep();
    SECTION("Writer writes rules") {
        Rule rules[] = {
            {Head_t::Disjunctive, {1}, Body_t::Normal, BOUND_NONE, {{-2, 1}, {3, 1}, {-4, 1}}},
            {Head_t::Disjunctive, {1, 2, 3}, Body_t::Normal, BOUND_NONE, {{5, 1}, {-6, 1}}},
            {Head_t::Disjunctive, {}, Body_t::Normal, BOUND_NONE, {{1, 1}, {2, 1}}},
            {Head_t::Choice, {1, 2, 3}, Body_t::Normal, BOUND_NONE, {{5, 1}, {-6, 1}}},
            // weight
            {Head_t::Disjunctive, {1}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
            {Head_t::Disjunctive, {2}, Body_t::Sum, 1, {{3, 1}, {-4, 1}, {5, 1}}},
            // mixed
            {Head_t::Choice, {1, 2}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
            {Head_t::Disjunctive, {}, Body_t::Sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
        };
        Vec<Lit_t> temp;
        for (auto&& r : rules) {
            if (r.bt == Body_t::Normal) {
                temp.clear();
                std::transform(r.body.begin(), r.body.end(), std::back_inserter(temp),
                               [](const WeightLit_t& x) { return x.lit; });
                writer.rule(r.ht, r.head, temp);
            }
            else {
                writer.rule(r.ht, r.head, r.bnd, r.body);
            }
        }
        writer.endStep();
        readAspif(out, observer);
        for (auto&& r : rules) {
            REQUIRE(std::find(observer.rules.begin(), observer.rules.end(), r) != observer.rules.end());
        }
    }
    SECTION("Writer writes minimize") {
        auto m1 = Vec<WeightLit_t>{{1, -2}, {-3, 2}, {4, 1}};
        auto m2 = Vec<WeightLit_t>{{-10, 1}, {-20, 2}};
        writer.minimize(1, m1);
        writer.minimize(-2, m2);
        writer.endStep();
        readAspif(out, observer);
        REQUIRE(observer.min.size() == 2);
        REQUIRE(observer.min[0].first == 1);
        REQUIRE(observer.min[1].first == -2);
        REQUIRE(observer.min[0].second == m1);
        REQUIRE(observer.min[1].second == m2);
    }
    SECTION("Writer writes output") {
        std::pair<std::string, Vec<Lit_t>> exp[] = {{"Hallo", {1, -2, 3}}, {"Fact", {}}};
        for (auto&& s : exp) { writer.output(s.first, s.second); }
        writer.endStep();
        readAspif(out, observer);
        for (auto&& s : exp) {
            REQUIRE(std::find(observer.shows.begin(), observer.shows.end(), s) != observer.shows.end());
        }
    }
    SECTION("Writer writes external") {
        std::pair<Atom_t, Value_t> exp[] = {
            {1, Value_t::Free}, {2, Value_t::True}, {3, Value_t::False}, {4, Value_t::Release}};
        for (auto&& e : exp) { writer.external(e.first, e.second); }
        writer.endStep();
        readAspif(out, observer);
        for (auto&& e : exp) {
            REQUIRE(std::find(observer.externals.begin(), observer.externals.end(), e) != observer.externals.end());
        }
    }
    SECTION("Writer writes assumptions") {
        Lit_t a[] = {1, 987232, -2};
        writer.assume({a, 2});
        writer.assume({a + 2, 1});
        writer.endStep();
        readAspif(out, observer);
        REQUIRE(spanEq(observer.assumes, std::span{a, 3}));
    }
    SECTION("Writer writes projection") {
        Atom_t a[] = {1, 987232, 2};
        writer.project({a, 2});
        writer.project({a + 2, 1});
        writer.endStep();
        readAspif(out, observer);
        REQUIRE(spanEq(observer.projects, std::span{a, 3}));
    }
    SECTION("Writer writes acyc edges") {
        Edge exp[] = {{0, 1, {1, -2}}, {1, 0, {3}}};
        for (auto&& e : exp) { writer.acycEdge(e.s, e.t, e.cond); }
        writer.endStep();
        readAspif(out, observer);
        REQUIRE(spanEq(observer.edges, std::span{exp, std::size(exp)}));
    }
    SECTION("Writer writes heuristics") {
        Heuristic exp[] = {{1, Heuristic_t::Sign, -1, 1, {10}},
                           {2, Heuristic_t::Level, 10, 3, {-1, 10}},
                           {1, Heuristic_t::Init, 20, 1, {}},
                           {1, Heuristic_t::Factor, 2, 2, {}}};
        for (auto&& h : exp) { writer.heuristic(h.atom, h.type, h.bias, h.prio, h.cond); }
        writer.endStep();
        readAspif(out, observer);
        REQUIRE(spanEq(observer.heuristics, std::span{exp, std::size(exp)}));
    }
}
TEST_CASE("TheoryData", "[aspif]") {
    TheoryData data;
    data.filter([](const TheoryAtom&) { return true; });
    SECTION("Destruct invalid term") {
        data.addTerm(10, "Foo");
        data.reset();
    }
    SECTION("Term 0 is ok") {
        data.addTerm(0, 0);
        REQUIRE(data.hasTerm(0));
        REQUIRE(data.getTerm(0).type() == Theory_t::Number);
    }

    SECTION("Visit theory") {
        Id_t tId = 0, s[3], n[5], o[2], f[3], e[4], args[2];
        // primitives
        data.addTerm(n[0] = tId++, 1);   // (number 1)
        data.addTerm(n[1] = tId++, 2);   // (number 2)
        data.addTerm(n[2] = tId++, 3);   // (number 3)
        data.addTerm(n[3] = tId++, 4);   // (number 4)
        data.addTerm(s[0] = tId++, "x"); // (string x)
        data.addTerm(s[1] = tId++, "z"); // (string z)
        // compounds
        data.addTerm(o[0] = tId++, "*");              // (operator *)
        data.addTerm(f[0] = tId++, s[0], {n, 1});     // (function x(1))
        data.addTerm(f[1] = tId++, s[0], {n + 1, 1}); // (function x(2))
        data.addTerm(f[2] = tId++, s[0], {n + 2, 1}); // (function x(3))
        args[0] = n[0];
        args[1] = f[0];
        data.addTerm(e[0] = tId++, o[0], {args, 2}); // (1 * x(1))
        args[0] = n[1];
        args[1] = f[1];
        data.addTerm(e[1] = tId++, o[0], {args, 2}); // (2 * x(2))
        args[0] = n[2];
        args[1] = f[2];
        data.addTerm(e[2] = tId++, o[0], {args, 2}); // (3 * x(3))
        args[0] = n[3];
        args[1] = s[1];
        data.addTerm(e[3] = tId++, o[0], {args, 2}); // (4 * z)
        // elements
        Id_t elems[4];
        data.addElement(elems[0] = 0, {&e[0], 1}, 0u); // (element 1*x(1):)
        data.addElement(elems[1] = 1, {&e[1], 1}, 0u); // (element 2*x(2):)
        data.addElement(elems[2] = 2, {&e[2], 1}, 0u); // (element 3*x(3):)
        data.addElement(elems[3] = 3, {&e[3], 1}, 0u); // (element 4*z:)

        // atom
        data.addTerm(s[2] = tId++, "sum");             // (string sum)
        data.addTerm(o[1] = tId++, ">=");              // (string >=)
        data.addTerm(n[4] = tId++, 42);                // (number 42)
        data.addAtom(1, s[2], {elems, 4}, o[1], n[4]); // (&sum { 1*x(1); 2*x(2); 3*x(3); 4*z     } >= 42)

        struct Visitor : public TheoryData::Visitor {
            void visit(const TheoryData& data, Id_t termId, const TheoryTerm& t) override {
                if (out.hasTerm(termId))
                    return;
                switch (t.type()) {
                    case Potassco::Theory_t::Number: out.addTerm(termId, t.number()); break;
                    case Potassco::Theory_t::Symbol: out.addTerm(termId, t.symbol()); break;
                    case Potassco::Theory_t::Compound:
                        data.accept(t, *this);
                        if (t.isFunction()) {
                            out.addTerm(termId, t.function(), t.terms());
                        }
                        else {
                            out.addTerm(termId, t.tuple(), t.terms());
                        }
                        break;
                    default: REQUIRE(false);
                }
            }
            void visit(const TheoryData& data, Id_t elemId, const TheoryElement& e) override {
                if (out.hasElement(elemId))
                    return;
                data.accept(e, *this);
                out.addElement(elemId, e.terms(), e.condition());
            }
            void visit(const TheoryData& data, const TheoryAtom& a) override {
                data.accept(a, *this);
                if (not a.guard()) {
                    out.addAtom(a.atom(), a.term(), a.elements());
                }
                else {
                    out.addAtom(a.atom(), a.term(), a.elements(), *a.guard(), *a.rhs());
                }
            }
            TheoryData out;
        } th;
        data.accept(th);
        REQUIRE(data.numAtoms() == th.out.numAtoms());
        for (Id_t id = 0; id != tId; ++id) {
            REQUIRE(data.hasTerm(id) == th.out.hasTerm(id));
            REQUIRE(data.getTerm(id).type() == th.out.getTerm(id).type());
        }
        for (Id_t id = 0; id != 4; ++id) { REQUIRE(data.hasElement(id) == th.out.hasElement(id)); }
    }
}

} // namespace Potassco::Test::Aspif
