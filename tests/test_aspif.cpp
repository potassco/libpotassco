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
constexpr Weight_t bound_none = -1;
static void        finalize(std::stringstream& str) { str << "0\n"; }
static void        rule(std::ostream& os, const Rule& r) {
    os << static_cast<unsigned>(AspifType::rule) << " " << static_cast<unsigned>(r.ht) << " ";
    os << r.head.size();
    for (auto x : r.head) { os << " " << x; }
    os << " " << static_cast<unsigned>(r.bt) << " ";
    if (r.bt == BodyType::sum) {
        os << r.bnd << " " << r.body.size();
        std::ranges::for_each(r.body, [&os](WeightLit x) { os << " " << x.lit << " " << x.weight; });
    }
    else {
        os << r.body.size();
        std::ranges::for_each(r.body, [&os](WeightLit x) { os << " " << x.lit; });
    }
    os << "\n";
}
static std::ostream& operator<<(std::ostream& os, const WeightLit& wl) {
    return os << "(" << wl.lit << "," << wl.weight << ")";
}
static std::ostream& operator<<(std::ostream& os, const std::pair<Atom_t, Potassco::TruthValue>& wl) {
    return os << "(" << wl.first << "," << static_cast<unsigned>(wl.second) << ")";
}
static std::ostream& operator<<(std::ostream& os, const Heuristic& h);
static std::ostream& operator<<(std::ostream& os, const Edge& e);
template <typename T>
static std::string stringify(const std::span<T>& s) {
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
template <Potassco::ScopedEnum E>
[[maybe_unused]] static std::ostream& operator<<(std::ostream& os, E e) {
    return os << Potassco::to_underlying(e);
}
namespace {
class ReadObserver final : public Test::ReadObserver {
public:
    void rule(HeadType ht, const AtomSpan& head, const LitSpan& body) override {
        rules.push_back({ht, {begin(head), end(head)}, BodyType::normal, bound_none, {}});
        Vec<WeightLit>& wb = rules.back().body;
        std::ranges::for_each(body, [&wb](Lit_t x) { wb.push_back({x, 1}); });
    }
    void rule(HeadType ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) override {
        rules.push_back({ht, {begin(head), end(head)}, BodyType::sum, bound, {begin(body), end(body)}});
    }
    void minimize(Weight_t prio, const WeightLitSpan& lits) override {
        min.push_back({prio, {begin(lits), end(lits)}});
    }
    void project(const AtomSpan& atoms) override { projects.insert(projects.end(), begin(atoms), end(atoms)); }
    void output(const std::string_view& str, const LitSpan& cond) override {
        shows.push_back({{begin(str), end(str)}, {begin(cond), end(cond)}});
    }

    void external(Atom_t a, TruthValue v) override { externals.emplace_back(a, v); }
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
    Vec<std::pair<int, Vec<WeightLit>>>     min;
    Vec<std::pair<std::string, Vec<Lit_t>>> shows;
    Vec<std::pair<Atom_t, TruthValue>>      externals;
    Vec<Atom_t>                             projects;
    Vec<Lit_t>                              assumes;
    TheoryData                              theory;
};

enum class DummyEnum : uint8_t {
    zero  = 0,
    one   = 1,
    two   = 2,
    three = 3,
    four  = 4,
    five  = 5,
    six   = 6,
    seven = 7,
    eight = 8,
};
POTASSCO_SET_DEFAULT_ENUM_MAX(DummyEnum::eight);
[[maybe_unused]] consteval auto enable_ops(std::type_identity<DummyEnum>) -> CmpOps { return {}; }

} // namespace

static unsigned compareRead(std::stringstream& input, ReadObserver& observer, const Rule* rules,
                            const std::pair<unsigned, unsigned>& subset) {
    for (unsigned i = 0; i != subset.second; ++i) { rule(input, rules[subset.first + i]); }
    finalize(input);
    readAspif(input, observer);
    if (observer.rules.size() != subset.second) {
        return static_cast<unsigned>(observer.rules.size());
    }
    for (unsigned i = 0; i != subset.second; ++i) {
        if (rules[subset.first + i] != observer.rules[i]) {
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
        static_assert(DynamicBuffer::trivially_relocatable::value);

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

        SECTION("memcpy") {
            DynamicBuffer empty;
            DynamicBuffer m2;
            void*         raw = m1.data();
            POTASSCO_WARNING_PUSH()
            POTASSCO_WARNING_IGNORE_GCC("-Wclass-memaccess")
            POTASSCO_WARNING_IGNORE_CLANG("-Wnontrivial-memaccess")
            std::memcpy(&m2, &m1, sizeof(DynamicBuffer));    // NOLINT(*-undefined-memory-manipulation)
            std::memcpy(&m1, &empty, sizeof(DynamicBuffer)); // NOLINT(*-undefined-memory-manipulation)
            POTASSCO_WARNING_POP()
            CHECK(raw == m2.data());
            CHECK(exp == m2.data());
            CHECK(m1.data() == nullptr);
        }
    }
}
TEST_CASE("Test ConstString", "[rule]") {
    SECTION("empty") {
        static_assert(sizeof(ConstString) == 24);
        ConstString s;
        REQUIRE(s.size() == 0);
        REQUIRE(s.c_str() != nullptr);
        REQUIRE_FALSE(*s.c_str());
    }
    SECTION("small to large") {
        std::string v;
        for (unsigned i = 0;; ++i) {
            v.assign(i, 'x');
            ConstString s(v);
            REQUIRE(v.size() == i);
            REQUIRE(s.size() == v.size());
            REQUIRE(std::strcmp(s.c_str(), v.c_str()) == 0);
            REQUIRE(static_cast<std::string_view>(s) == v);
            if (not s.small()) {
                break;
            }
        }
        REQUIRE(v.size() == 24);
    }
    SECTION("deep copy") {
        std::string_view sv("small");
        ConstString      s(sv);
        ConstString      s2(s);
        REQUIRE(s == sv);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s2 == sv);
        REQUIRE((void*) s.c_str() != (void*) s2.c_str());
        std::string large(32, 'x');
        ConstString s3(large);
        ConstString s4(s3);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s3 == std::string_view{large});
        REQUIRE(s4 == std::string_view{large});
        REQUIRE((void*) s3.c_str() != (void*) s4.c_str());

        SECTION("assign") {
            ConstString sc;
            sc = s3;
            REQUIRE(sc == std::string_view{large});
            REQUIRE((void*) sc.c_str() != (void*) s3.c_str());
            sc = s2;
            REQUIRE(sc == sv);
            REQUIRE((void*) sc.c_str() != (void*) s2.c_str());
        }
    }
    SECTION("shallow copy") {
        std::string_view sv("small");
        ConstString      s(sv, ConstString::create_shared);
        ConstString      s2(s);
        REQUIRE(s == sv);
        REQUIRE_FALSE(s.shareable());
        REQUIRE(s2 == sv);
        REQUIRE((void*) s.c_str() != (void*) s2.c_str());
        std::string large(32, 'x');
        ConstString s3(large, ConstString::create_shared);
        ConstString s4(s3);
        REQUIRE(s3.shareable());
        REQUIRE(s4.shareable());
        REQUIRE(s3 == std::string_view{large});
        REQUIRE(s4 == std::string_view{large});
        REQUIRE((void*) s3.c_str() == (void*) s4.c_str());

        SECTION("assign") {
            ConstString sc;
            sc = s3;
            REQUIRE(sc == std::string_view{large});
            REQUIRE((void*) sc.c_str() == (void*) s3.c_str());
            sc = s2;
            REQUIRE(sc == sv);
            REQUIRE((void*) sc.c_str() != (void*) s2.c_str());
        }
    }
    SECTION("move") {
        std::string_view sv("small");
        ConstString      s(sv);
        ConstString      s2(std::move(s));
        REQUIRE(s == std::string_view{});
        REQUIRE(s2 == sv);
        std::string large(32, 'x');
        ConstString s3(large);
        auto        old = static_cast<const void*>(s3.c_str());
        ConstString s4(std::move(s3));
        REQUIRE(s3 == std::string_view{});
        REQUIRE(s4 == std::string_view(large));
        REQUIRE(s4.c_str() == old);

        SECTION("assign") {
            s = std::move(s2);
            REQUIRE(s2 == std::string_view{});
            REQUIRE(s == sv);

            s3 = std::move(s4);
            REQUIRE(s4 == std::string_view{});
            REQUIRE(s3 == std::string_view(large));

            s3 = ConstString();
            REQUIRE(s3 == std::string_view{});
        }
    }
    SECTION("compare") {
        ConstString s("one");
        ConstString t("two");
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

TEST_CASE("Test Basic", "[rule]") {
    SECTION("atom") {
        static_assert(std::is_same_v<Atom_t, uint32_t>);
        static_assert(atom_min > 0);
        static_assert(atom_min < UINT32_MAX);
        auto x = static_cast<Atom_t>(7);
        CHECK(weight(x) == 1);
        CHECK(validAtom(atom_min));
        CHECK(validAtom(atom_max));
        CHECK_FALSE(validAtom(atom_min - 1));
        CHECK_FALSE(validAtom(atom_max + 1));
        CHECK_FALSE(validAtom(-400));
    }
    SECTION("literal") {
        static_assert(std::is_same_v<Lit_t, int32_t>);
        auto x = static_cast<Lit_t>(7);
        CHECK(weight(x) == 1);
        CHECK(atom(x) == 7);
        CHECK(neg(x) == -7);
        CHECK(validAtom(x));
        CHECK_FALSE(validAtom(neg(x)));
    }
    SECTION("weight literal") {
        WeightLit wl{.lit = -4, .weight = 3};
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
    SECTION("enums") {
        using std::literals::operator""sv;
        CHECK(enum_min<AspifType>() == 0);
        CHECK(enum_max<AspifType>() == 10);
        CHECK(enum_count<AspifType>() == 11);

        CHECK(enum_min<HeadType>() == 0);
        CHECK(enum_max<HeadType>() == 1);
        CHECK(enum_count<HeadType>() == 2);

        CHECK(enum_min<BodyType>() == 0);
        CHECK(enum_max<BodyType>() == 2);
        CHECK(enum_count<BodyType>() == 3);

        CHECK(enum_min<TruthValue>() == 0);
        CHECK(enum_max<TruthValue>() == 3);
        CHECK(enum_count<TruthValue>() == 4);
        static_assert(enum_name(TruthValue::false_) == "false"sv);
        static_assert(enum_name(TruthValue::release) == "release"sv);

        CHECK(enum_min<DomModifier>() == 0);
        CHECK(enum_max<DomModifier>() == 5);
        CHECK(enum_count<DomModifier>() == 6);

        static_assert(enum_name(DomModifier::init) == "init"sv);
        static_assert(enum_name(DomModifier::level) == "level"sv);

        CHECK(enum_min<TheoryTermType>() == 0);
        CHECK(enum_max<TheoryTermType>() == 2);
        CHECK(enum_count<TheoryTermType>() == 3);

        CHECK(enum_min<TheoryType>() == 0);
        CHECK(enum_max<TheoryType>() == 6);
        CHECK(enum_count<TheoryType>() == 7);

        CHECK(enum_min<TupleType>() == -3);
        CHECK(enum_max<TupleType>() == -1);
        CHECK(enum_count<TupleType>() == 3);
    }
    SECTION("bits") {
        unsigned n = 0;
        CHECK(store_set_bit(n, 2u) == 4u);
        CHECK(test_bit(n, 2u));
        CHECK(store_toggle_bit(n, 3u) == 12u);
        CHECK(test_bit(n, 3u));
        CHECK(store_toggle_bit(n, 2u) == 8u);
        CHECK_FALSE(test_bit(n, 2u));
        CHECK(store_clear_bit(n, 3u) == 0u);
        CHECK(n == 0);
    }
    SECTION("bitset") {
        Bitset<unsigned> bs({1u, 2u, 5u});
        CHECK(bs.count() == 3);
        CHECK(bs.contains(1));
        CHECK(bs.contains(2));
        CHECK(bs.contains(5));
        CHECK_FALSE(bs.contains(0));
        CHECK_FALSE(bs.contains(3));
        CHECK_FALSE(bs.contains(4));

        bs.removeMax(5);
        CHECK_FALSE(bs.contains(5));
        CHECK(bs.count() == 2);
        bs.add(3);
        bs.add(4);
        bs.add(5);
        CHECK(bs.count() == 5);
        bs.removeMax(4);
        CHECK_FALSE(bs.contains(5));
        CHECK_FALSE(bs.contains(4));
        CHECK(bs.contains(3));
        CHECK(bs.count() == 3);
        bs.remove(3);
        CHECK(bs.count() == 2);
        CHECK_FALSE(bs.contains(3));

        auto copy = bs;
        bs.removeMax(0);
        CHECK(bs.count() == 0);
        CHECK(copy.count() == 2);
        copy.clear();
        CHECK(copy.count() == 0);

        bs.add(31);
        bs.add(30);
        CHECK(bs.count() == 2);
        bs.removeMax(32);
        CHECK(bs.count() == 2);
        bs.removeMax(31);
        CHECK(bs.count() == 1);
    }

    SECTION("bitset enum") {
        using SetType = Bitset<unsigned, DummyEnum>;
        static_assert(sizeof(SetType) == sizeof(unsigned));

        SetType dummy;
        dummy.add(DummyEnum::eight);
        CHECK(dummy.count() == 1u);
        CHECK(dummy.contains(DummyEnum::eight));
        CHECK_FALSE(dummy.contains(DummyEnum::seven));

        dummy.add(DummyEnum::five);
        dummy.removeMax(DummyEnum::seven);
        CHECK(dummy.count() == 1u);
        CHECK(dummy.contains(DummyEnum::five));
    }

    SECTION("dynamic biset") {
        DynamicBitset bitset;
        CHECK(bitset.count() == 0);
        CHECK(bitset == bitset);
        CHECK_FALSE(bitset < bitset);
        CHECK_FALSE(bitset > bitset);
        CHECK(bitset <= bitset);

        bitset.add(63);
        DynamicBitset other;
        CHECK(bitset.count() == 1);
        CHECK(other < bitset);
        bitset.add(64);
        CHECK(bitset.count() == 2);
        other.add(64);
        CHECK(other < bitset);
        other.add(65);
        CHECK(other > bitset);
        other.add(128);
        CHECK(other > bitset);
        CHECK(other.count() == 3);
        other.remove(65);
        CHECK(other > bitset);
        other.add(63);
        other.remove(128);
        CHECK(other == bitset);
        other.add(4096);
        other.add(100000);
        CHECK(other.count() == 4);
        other.remove(100000);
        CHECK(other.count() == 3);
    }
}
TEST_CASE("Test RuleBuilder", "[rule]") {
    RuleBuilder rb;
    static_assert(RuleBuilder::trivially_relocatable::value);

    SECTION("simple rule") {
        rb.start().addHead(1).addGoal(2).addGoal(-3).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector{2, -3}));
    }
    SECTION("simple constraint") {
        SECTION("head first") { rb.start().addGoal(2).addGoal(-3).end(); }
        SECTION("body first") { rb.startBody().addGoal(2).addGoal(-3).start().end(); }
        CHECK(spanEq(rb.head(), std::vector<Atom_t>{}));
        CHECK(rb.bodyType() == BodyType::normal);
        CHECK(spanEq(rb.body(), std::vector{2, -3}));
    }
    SECTION("simple choice") {
        SECTION("head first") { rb.start(HeadType::choice).addHead(1).addHead(2).startBody().end(); }
        SECTION("body first") { rb.startBody().start(HeadType::choice).addHead(1).addHead(2).end(); }
        CHECK(spanEq(rb.head(), std::vector<Atom_t>{1, 2}));
        CHECK(rb.bodyType() == BodyType::normal);
        CHECK(spanEq(rb.body(), std::vector<Lit_t>{}));
    }
    SECTION("simple weight rule") {
        rb.start().addHead(1).startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(rb.bound() == 2);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{2, 1}, {-3, 1}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
        REQUIRE(rb.findSumLit(4)->weight == 2);
        REQUIRE(rb.findSumLit(-4) == nullptr);

        auto r = rb.rule();
        REQUIRE(spanEq(r.head, rb.head()));
        REQUIRE(r.bt == BodyType::sum);
        REQUIRE(r.agg.bound == 2);
        REQUIRE(spanEq(r.agg.lits, rb.sum().lits));
    }
    SECTION("update bound") {
        rb.start().addHead(1).startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).setBound(3);
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(rb.bound() == 3);
        rb.clear();
        rb.startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).addHead(1).setBound(4);
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(rb.bound() == 4);
        rb.clear();
        rb.startSum(2).addGoal(2, 1).addGoal(-3, 1).addGoal(4, 2).addHead(1).end();
        REQUIRE_THROWS_AS(rb.setBound(4), std::logic_error);
    }
    SECTION("weaken to cardinality rule") {
        rb.start().addHead(1).startSum(2).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).weaken(BodyType::count).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::count);
        REQUIRE(rb.bound() == 1);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{2, 1}, {-3, 1}, {4, 1}}));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
        auto r = rb.rule();
        REQUIRE(spanEq(r.head, rb.head()));
        REQUIRE(r.bt == BodyType::count);
        REQUIRE(r.agg.bound == 1);
        REQUIRE(spanEq(r.agg.lits, rb.sum().lits));
    }
    SECTION("weaken to normal rule") {
        rb.start().addHead(1).startSum(3).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).weaken(BodyType::normal).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{2, -3, 4}));

        auto r = rb.rule();
        REQUIRE(spanEq(r.head, rb.head()));
        REQUIRE(r.bt == BodyType::normal);
        REQUIRE(spanEq(r.cond, rb.body()));
    }
    SECTION("weak to normal rule - inverse order") {
        rb.startSum(3).addGoal(2, 2).addGoal(-3, 2).addGoal(4, 2).start().addHead(1).weaken(BodyType::normal).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{2, -3, 4}));
    }
    SECTION("minimize rule") {
        SECTION("implicit body") {
            rb.startMinimize(1).addGoal(-3, 2).addGoal(4, 1).addGoal(5).end();
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{}));
            REQUIRE(rb.isMinimize());
            REQUIRE(rb.bodyType() == BodyType::sum);
            REQUIRE(rb.bound() == 1);
            REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{-3, 2}, {4, 1}, {5, 1}}));
            REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
        }
        SECTION("explicit body") {
            rb.startMinimize(1).startSum(0).addGoal(-3, 2).addGoal(4, 1).addGoal(5).end();
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{}));
            REQUIRE(rb.isMinimize());
            REQUIRE(rb.bodyType() == BodyType::sum);
            REQUIRE(rb.bound() == 1);
            REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{-3, 2}, {4, 1}, {5, 1}}));
            REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
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
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{5}));

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
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector<Lit_t>{5}));
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
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{2, 2}, {-3, 2}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));

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
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{2, 2}, {-3, 2}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
    }

    SECTION("copy and move") {
        rb.start().addHead(1).startSum(25);
        std::vector<WeightLit> exp;
        for (int i = 2; i != 20; ++i) {
            auto wl = WeightLit{(i & 1) ? i : -i, i};
            rb.addGoal(wl);
            exp.push_back(wl);
        }
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.sum().bound == 25);
        REQUIRE(spanEq(rb.sumLits(), exp));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));

        SECTION("copy") {
            RuleBuilder copy(rb);
            REQUIRE(spanEq(copy.head(), std::vector<Atom_t>{1}));
            REQUIRE(copy.sum().bound == 25);
            REQUIRE(spanEq(copy.sumLits(), exp));

            auto newLit = WeightLit{4711, 31};
            copy.addGoal(newLit);
            REQUIRE(spanEq(rb.sumLits(), exp));

            exp.push_back(newLit);
            REQUIRE(spanEq(copy.sumLits(), exp));
        }
        SECTION("move") {
            RuleBuilder mv(std::move(rb));
            REQUIRE(spanEq(mv.head(), std::vector<Atom_t>{1}));
            REQUIRE(mv.sum().bound == 25);
            REQUIRE(spanEq(mv.sumLits(), exp));

            REQUIRE(rb.head().empty());
            REQUIRE(rb.body().empty());

            rb.start().addHead(1).addGoal(2).addGoal(-3).end();
            REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
            REQUIRE(rb.bodyType() == BodyType::normal);
            REQUIRE(spanEq(rb.body(), std::vector{2, -3}));
        }
        SECTION("memcpy") {
            RuleBuilder empty;
            RuleBuilder mc;
            POTASSCO_WARNING_PUSH()
            POTASSCO_WARNING_IGNORE_GCC("-Wclass-memaccess")
            POTASSCO_WARNING_IGNORE_CLANG("-Wnontrivial-memaccess")
            std::memcpy(&mc, &rb, sizeof(RuleBuilder));    // NOLINT(*-undefined-memory-manipulation)
            std::memcpy(&rb, &empty, sizeof(RuleBuilder)); // NOLINT(*-undefined-memory-manipulation)
            POTASSCO_WARNING_POP()

            REQUIRE(spanEq(mc.head(), std::vector<Atom_t>{1}));
            REQUIRE(mc.sum().bound == 25);
            REQUIRE(spanEq(mc.sumLits(), exp));
            auto newLit = WeightLit{4711, 31};
            mc.addGoal(newLit);
            exp.push_back(newLit);
            REQUIRE(spanEq(mc.sumLits(), exp));
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
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(rb.bound() == 22);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{47, 11}, {18, 15}, {17, 7}}));
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
        rule(input, {HeadType::disjunctive, {}, BodyType::normal, bound_none, {}});
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.rules.size() == 1);
        REQUIRE(observer.rules[0].head.empty());
        REQUIRE(observer.rules[0].body.empty());
    }
    SECTION("read rules") {
        Rule rules[] = {{HeadType::disjunctive, {1}, BodyType::normal, bound_none, {{-2, 1}, {3, 1}, {-4, 1}}},
                        {HeadType::disjunctive, {1, 2, 3}, BodyType::normal, bound_none, {{5, 1}, {-6, 1}}},
                        {HeadType::disjunctive, {}, BodyType::normal, bound_none, {{1, 1}, {2, 1}}},
                        {HeadType::choice, {1, 2, 3}, BodyType::normal, bound_none, {{5, 1}, {-6, 1}}},
                        // weight
                        {HeadType::disjunctive, {1}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        {HeadType::disjunctive, {2}, BodyType::sum, 1, {{3, 1}, {-4, 1}, {5, 1}}},
                        // mixed
                        {HeadType::choice, {1, 2}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        {HeadType::disjunctive, {}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
                        // negative weights
                        {HeadType::disjunctive, {1}, BodyType::sum, 1, {{2, 1}, {-3, -2}, {-4, 3}, {5, 1}}}};
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
        input << AspifType::minimize << " -1 3 4 5 6 1 3 2\n";
        input << AspifType::minimize << " 10 3 4 -52 -6 36 3 -20\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.min.size() == 2);
        const auto& mr1 = observer.min[0];
        const auto& mr2 = observer.min[1];
        REQUIRE(mr1.first == -1);
        REQUIRE(mr2.first == 10);
        auto lits = Vec<WeightLit>{{4, 5}, {6, 1}, {3, 2}};
        REQUIRE(mr1.second == lits);
        lits = Vec<WeightLit>{{4, -52}, {-6, 36}, {3, -20}};
        REQUIRE(mr2.second == lits);
    }
    SECTION("read output") {
        input << AspifType::output << " 1 a 1 1\n";
        input << AspifType::output << " 10 Hallo Welt 2 1 -2\n";
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
        input << AspifType::project << " 3 1 2 987232\n";
        input << AspifType::project << " 1 17\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.projects == Vec<Atom_t>({1, 2, 987232, 17}));
    }
    SECTION("read external") {
        std::pair<Atom_t, TruthValue> exp[] = {
            {1, TruthValue::free}, {2, TruthValue::true_}, {3, TruthValue::false_}, {4, TruthValue::release}};
        for (auto&& e : exp) { input << AspifType::external << " " << e.first << " " << e.second << "\n"; }
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(spanEq(observer.externals, std::span{exp, std::size(exp)}));
    }
    SECTION("read assumptions") {
        input << AspifType::assume << " 2 1 987232\n";
        input << AspifType::assume << " 1 -2\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.assumes == Vec<Lit_t>({1, 987232, -2}));
    }
    SECTION("read edges") {
        input << AspifType::edge << " 0 1 2 1 -2\n";
        input << AspifType::edge << " 1 0 1 3\n";
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
        Heuristic exp[] = {{1, DomModifier::sign, -1, 1, {10}},
                           {2, DomModifier::level, 10, 3, {-1, 10}},
                           {1, DomModifier::init, 20, 1, {}},
                           {1, DomModifier::factor, 2, 2, {}}};
        for (auto&& r : exp) {
            input << AspifType::heuristic << " " << r.type << " " << r.atom << " " << r.bias << " " << r.prio << " "
                  << r.cond.size();
            for (auto&& p : r.cond) { input << " " << p; }
            input << "\n";
        }
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(spanEq(observer.heuristics, std::span{exp, std::size(exp)}));
    }
    SECTION("read theory") {
        input << AspifType::theory << " 0 1 200\n"
              << AspifType::theory << " 0 6 1\n"
              << AspifType::theory << " 0 11 2\n"
              << AspifType::theory << " 1 0 4 diff\n"
              << AspifType::theory << " 1 2 2 <=\n"
              << AspifType::theory << " 1 4 1 -\n"
              << AspifType::theory << " 1 5 3 end\n"
              << AspifType::theory << " 1 8 5 start\n"
              << AspifType::theory << " 2 10 4 2 7 9\n"
              << AspifType::theory << " 2 7 5 1 6\n"
              << AspifType::theory << " 2 9 8 1 6\n"
              << AspifType::theory << " 4 0 1 10 0\n"
              << AspifType::theory << " 6 0 0 1 0 2 1\n";
        finalize(input);
        REQUIRE(readAspif(input, observer) == 0);
        REQUIRE(observer.theory.numAtoms() == 1);

        class AtomVisitor : public TheoryData::Visitor {
        public:
            void visit(const TheoryData& data, Id_t, const TheoryTerm& t) override {
                if (auto type = t.type(); type == TheoryTermType::number) {
                    out << t.number();
                }
                else if (type == TheoryTermType::symbol) {
                    out << t.symbol();
                }
                else if (t.isFunction()) {
                    function(data, t);
                }
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
        input << AspifType::comment << "Hello World" << "\n";
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
        rule(input, {HeadType::disjunctive, {1, 2}, BodyType::normal, bound_none, {}});
        finalize(input);
        rule(input, {HeadType::disjunctive, {3, 4}, BodyType::normal, bound_none, {}});
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
            {HeadType::disjunctive, {1}, BodyType::normal, bound_none, {{-2, 1}, {3, 1}, {-4, 1}}},
            {HeadType::disjunctive, {1, 2, 3}, BodyType::normal, bound_none, {{5, 1}, {-6, 1}}},
            {HeadType::disjunctive, {}, BodyType::normal, bound_none, {{1, 1}, {2, 1}}},
            {HeadType::choice, {1, 2, 3}, BodyType::normal, bound_none, {{5, 1}, {-6, 1}}},
            // weight
            {HeadType::disjunctive, {1}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
            {HeadType::disjunctive, {2}, BodyType::sum, 1, {{3, 1}, {-4, 1}, {5, 1}}},
            // mixed
            {HeadType::choice, {1, 2}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
            {HeadType::disjunctive, {}, BodyType::sum, 1, {{2, 1}, {-3, 2}, {-4, 3}, {5, 1}}},
        };
        Vec<Lit_t> temp;
        for (auto&& r : rules) {
            if (r.bt == BodyType::normal) {
                temp.clear();
                std::ranges::transform(r.body, std::back_inserter(temp), [](const WeightLit& x) { return x.lit; });
                writer.rule(r.ht, r.head, temp);
            }
            else {
                writer.rule(r.ht, r.head, r.bnd, r.body);
            }
        }
        writer.endStep();
        readAspif(out, observer);
        for (auto&& r : rules) { REQUIRE(std::ranges::find(observer.rules, r) != observer.rules.end()); }
    }
    SECTION("Writer writes minimize") {
        auto m1 = Vec<WeightLit>{{1, -2}, {-3, 2}, {4, 1}};
        auto m2 = Vec<WeightLit>{{-10, 1}, {-20, 2}};
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
        for (auto&& s : exp) { REQUIRE(std::ranges::find(observer.shows, s) != observer.shows.end()); }
    }
    SECTION("Writer writes external") {
        std::pair<Atom_t, TruthValue> exp[] = {
            {1, TruthValue::free}, {2, TruthValue::true_}, {3, TruthValue::false_}, {4, TruthValue::release}};
        for (auto&& e : exp) { writer.external(e.first, e.second); }
        writer.endStep();
        readAspif(out, observer);
        for (auto&& e : exp) { REQUIRE(std::ranges::find(observer.externals, e) != observer.externals.end()); }
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
        Heuristic exp[] = {{1, DomModifier::sign, -1, 1, {10}},
                           {2, DomModifier::level, 10, 3, {-1, 10}},
                           {1, DomModifier::init, 20, 1, {}},
                           {1, DomModifier::factor, 2, 2, {}}};
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
        REQUIRE(data.getTerm(0).type() == TheoryTermType::number);
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
        data.addElement(elems[0] = 0, toSpan(e[0]), 0u); // (element 1*x(1):)
        data.addElement(elems[1] = 1, toSpan(e[1]), 0u); // (element 2*x(2):)
        data.addElement(elems[2] = 2, toSpan(e[2]), 0u); // (element 3*x(3):)
        data.addElement(elems[3] = 3, toSpan(e[3]), 0u); // (element 4*z:)

        // atom
        data.addTerm(s[2] = tId++, "sum");             // (string sum)
        data.addTerm(o[1] = tId++, ">=");              // (string >=)
        data.addTerm(n[4] = tId++, 42);                // (number 42)
        data.addAtom(1, s[2], {elems, 4}, o[1], n[4]); // (&sum { 1*x(1); 2*x(2); 3*x(3); 4*z     } >= 42)

        struct Visitor : TheoryData::Visitor {
            void visit(const TheoryData& data, Id_t termId, const TheoryTerm& t) override {
                if (out.hasTerm(termId)) {
                    return;
                }
                switch (t.type()) {
                    case TheoryTermType::number: out.addTerm(termId, t.number()); break;
                    case TheoryTermType::symbol: out.addTerm(termId, t.symbol()); break;
                    case TheoryTermType::compound:
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
                if (out.hasElement(elemId)) {
                    return;
                }
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
