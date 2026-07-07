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
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <map>
#include <random>
#include <ranges>
#include <sstream>
#include <unordered_map>
using namespace std::literals;
namespace Potassco::Test::Aspif {
constexpr Weight_t   bound_none = -1;
static std::ostream& operator<<(std::ostream& os, const Heuristic& h);
static std::ostream& operator<<(std::ostream& os, const WeightLit& wl) {
    return os << "(" << wl.lit << "," << wl.weight << ")";
}
template <ScopedEnum E>
[[maybe_unused]] static std::ostream& operator<<(std::ostream& os, E e) {
    return os << Potassco::to_underlying(e);
}
static std::ostream& operator<<(std::ostream& os, const std::pair<Atom_t, TruthValue>& wl) {
    return os << "(" << wl.first << "," << static_cast<unsigned>(wl.second) << ")";
}
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
template <typename T, typename U>
static bool spanEq(const T& lhs, const U& rhs) {
    if (std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end())) {
        return true;
    }
    UNSCOPED_INFO("LHS: " << stringify(std::span(lhs)) << "\n  RHS: " << stringify(std::span(rhs)));
    return false;
}
static void toAspif(std::ostream& os, const Rule& r) {
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

namespace {
class ReadObserver final : public Test::ReadObserver {
public:
    void rule(HeadType ht, AtomSpan head, LitSpan body) override {
        rules.push_back({ht, {begin(head), end(head)}, BodyType::normal, bound_none, {}});
        Vec<WeightLit>& wb = rules.back().body;
        std::ranges::for_each(body, [&wb](Lit_t x) { wb.push_back({x, 1}); });
    }
    void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) override {
        rules.push_back({ht, {begin(head), end(head)}, BodyType::sum, bound, {begin(body), end(body)}});
    }
    void minimize(Weight_t prio, WeightLitSpan lits) override { min.push_back({prio, {begin(lits), end(lits)}}); }
    void project(AtomSpan project) override { projects.insert(projects.end(), begin(project), end(project)); }
    void outputTerm(Id_t termId, std::string_view name) override { shows[termId].first = name; }
    void output(Id_t termId, LitSpan cond) override { shows[termId].second.emplace_back(begin(cond), end(cond)); }

    void external(Atom_t a, TruthValue v) override { externals.emplace_back(a, v); }
    void assume(LitSpan lits) override { assumes.insert(assumes.end(), begin(lits), end(lits)); }
    void theoryTerm(Id_t termId, int number) override { theory.addTerm(termId, number); }
    void theoryTerm(Id_t termId, std::string_view name) override { theory.addTerm(termId, name); }
    void theoryTerm(Id_t termId, int cId, IdSpan args) override {
        theory.addTerm(termId, static_cast<Id_t>(cId), args);
    }
    void theoryElement(Id_t elementId, IdSpan terms, LitSpan) override { theory.addElement(elementId, terms, 0u); }
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) override {
        theory.addAtom(atomOrZero, termId, elements);
    }
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) override {
        theory.addAtom(atomOrZero, termId, elements, op, rhs);
    }
    using TermMap = std::unordered_map<unsigned, std::pair<std::string, Vec<Vec<Lit_t>>>>;
    Vec<Rule>                           rules;
    Vec<std::pair<int, Vec<WeightLit>>> min;
    TermMap                             shows;
    Vec<std::pair<Atom_t, TruthValue>>  externals;
    Vec<Atom_t>                         projects;
    Vec<Lit_t>                          assumes;
    TheoryData                          theory;
};

enum class DummyEnum : uint8_t { five = 5, seven = 7, eight = 8 };
POTASSCO_SET_DEFAULT_ENUM_MAX(DummyEnum::eight);
POTASSCO_ENABLE_CMP_OPS(DummyEnum);

} // namespace

TEST_CASE("Test BufferedStream", "[input]") {
    SECTION("read bug") {
        std::stringstream data("Foo");
        BufferedStream    str(data);
        std::string       out(10, 'x');
        auto              r = str.read(std::span{out.data(), out.size()});
        out.resize(r);
        CHECK(r == 3);
        CHECK(out == "Foo");
    }
    SECTION("match") {
        std::stringstream data("Hello World!");
        BufferedStream    str(data);
        CHECK(str.match("Hello"));
        CHECK(str.get() == ' ');
        CHECK_FALSE(str.match("World!!"));
        CHECK(str.match("World!"));
        CHECK_FALSE(str.peek());
    }
    SECTION("matchLong") {
        std::stringstream data;
        data.str(std::string(4050, 'x').append(200, 'y').append(100, 'z'));
        BufferedStream str(data);
        std::string    x(4020, '-');
        REQUIRE(str.read(std::span{x.data(), x.size()}) == x.size());
        REQUIRE(x == std::string(4020, 'x'));
        x.assign(30, 'x').append(198, 'y');
        REQUIRE(str.match(x));
        x.assign(2, 'y').append(95, 'z');
        REQUIRE(str.match(x));
        x.assign(20, '-');
        REQUIRE(str.read(std::span{x.data(), x.size()}) == 5);
        x.resize(5);
        REQUIRE(x.find_first_not_of('z') == std::string::npos);
        REQUIRE_FALSE(str.peek());
    }
    SECTION("unget") {
        std::stringstream data("Foo");
        BufferedStream    str(data);
        CHECK_FALSE(str.unget('x'));
        CHECK(str.get() == 'F');
        CHECK(str.unget('x'));
        CHECK(str.get() == 'x');
        CHECK(str.get() == 'o');
        CHECK(str.unget('h'));
        CHECK(str.unget('W'));
        CHECK(str.match("Who"));
        CHECK_FALSE(str.peek());
        CHECK(str.unget('!'));
        CHECK(str.peek() == '!');
    }
    SECTION("translate newline") {
        std::stringstream data;
        SECTION("simple") {
            auto nl   = GENERATE("\r", "\r\n");
            auto term = GENERATE(false, true);
            CAPTURE(nl);
            CAPTURE(term);
            data << "line1" << nl << "line2" << nl << "line3";
            if (term) {
                data << nl;
            }
            BufferedStream str(data);
            for (auto x : "line1\nline2\nline3"s) { REQUIRE(str.get() == x); }
            REQUIRE(str.line() == 3);
            if (term) {
                REQUIRE(str.get() == '\n');
            }
            REQUIRE_FALSE(str.peek());
        }
    }
    SECTION("readLine") {
        DynamicBuffer     buffer;
        std::stringstream data;
        SECTION("stop at any newline") {
            auto nl = GENERATE("\n", "\r", "\r\n");
            data << "Foo" << nl << "x";
            BufferedStream str(data);
            CAPTURE(nl);
            CHECK(str.readLine(buffer) == 3u);
            REQUIRE(buffer.view() == "Foo");
            CHECK(str.line() == 2);
            REQUIRE(str.get() == 'x');
        }
        SECTION("eof") {
            data << "Foo";
            BufferedStream str(data);
            CHECK(str.readLine(buffer) == 3u);
            REQUIRE(buffer.view() == "Foo");
            CHECK(str.line() == 1);
            CHECK_FALSE(str.peek());
        }
        SECTION("control") {
            data << "Foo\t\tBar\nx";
            BufferedStream str(data);
            CHECK(str.readLine(buffer) == 8u);
            REQUIRE(buffer.view() == "Foo\t\tBar");
            CHECK(str.line() == 2);
            CHECK(str.peek() == 'x');
        }
        SECTION("underflow") {
            std::string expected(6000, 'x');
            data << expected << "\nstop";
            BufferedStream str(data);
            CHECK(str.readLine(buffer) == expected.size());
            REQUIRE(buffer.view() == expected);
            CHECK(str.match("stop"));
        }
    }
    SECTION("skipLine") {
        std::stringstream data;
        SECTION("stop at any newline") {
            auto nl = GENERATE("\n", "\r", "\r\n");
            data << "Foo" << nl << "x";
            BufferedStream str(data);
            CAPTURE(nl);
            str.skipLine();
            CHECK(str.line() == 2);
            REQUIRE(str.get() == 'x');
        }
        SECTION("eof") {
            data << "Foo";
            BufferedStream str(data);
            str.skipLine();
            CHECK(str.line() == 1);
            CHECK_FALSE(str.peek());
        }
        SECTION("control") {
            data << "Foo\t\tBar\nx";
            BufferedStream str(data);
            str.skipLine();
            CHECK(str.line() == 2);
            CHECK(str.peek() == 'x');
        }
        SECTION("underflow") {
            data << std::string(6000, 'x') << "\nstop";
            BufferedStream str(data);
            str.skipLine();
            CHECK(str.match("stop"));
        }
    }
}
TEST_CASE("Test DynamicBuffer", "[util]") {
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
    SECTION("supports borrow") {
        char          buffer[5];
        DynamicBuffer r(std::span{buffer});
        REQUIRE(r.capacity() == 5);
        REQUIRE(r.size() == 0);
        REQUIRE(r.data() == buffer);
        std::memset(r.alloc(4).data(), 'A', 4);
        std::string exp;
        exp.append(4, 'A');
        REQUIRE(r.view() == exp);
        REQUIRE(r.size() == 4);
        REQUIRE(r.data() == buffer);
        std::memset(r.alloc(1).data(), 'B', 1);
        exp.push_back('B');
        REQUIRE(r.view() == exp);
        REQUIRE(r.size() == 5);
        REQUIRE(r.data() == buffer);
        std::memset(r.alloc(1).data(), 'C', 1);
        exp.push_back('C');
        REQUIRE(r.view() == exp);
        REQUIRE(r.size() == 6);
        REQUIRE(r.data() != buffer);
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

        REQUIRE_THROWS_AS(r.reserve(r.maxSize() + 1), std::length_error);
        REQUIRE(r.capacity() >= (old * 2.0));
        REQUIRE(r.back() == c);
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
        static_assert(std::is_copy_assignable_v<DynamicBuffer>, "should be copyable");
        static_assert(std::is_copy_constructible_v<DynamicBuffer>, "should be copyable");
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
TEST_CASE("Test DynamicArray", "[util]") {
    static constexpr auto supportsArray = []<typename T>(std::type_identity<T>) {
        if constexpr (requires { typename DynamicArray<T>::value_type; }) {
            return std::true_type{};
        }
        else {
            return std::false_type{};
        }
    };
    STATIC_REQUIRE_FALSE(supportsArray(std::type_identity<std::string>{}));
    STATIC_REQUIRE(supportsArray(std::type_identity<ConstString>{}));
    SECTION("starts empty") {
        DynamicArray<int> r;
        REQUIRE(r.size() == 0);
        REQUIRE(r.capacity() == 0);
        REQUIRE(r.data() == nullptr);
        REQUIRE(r.empty());
    }
    SECTION("copy and move") {
        using StrArray = DynamicArray<ConstString>;
        static_assert(std::is_move_constructible_v<StrArray>, "should be movable");
        static_assert(std::is_move_assignable_v<StrArray>, "should be movable");
        static_assert(std::is_copy_assignable_v<StrArray>, "should be copyable");
        static_assert(std::is_copy_constructible_v<StrArray>, "should be copyable");
        static_assert(StrArray::trivially_relocatable::value);

        StrArray m1;
        auto     longStr = "A long long string longer than our SSO buffer size"sv;
        m1.emplace_back(longStr);
        m1.emplace_back("Short");
        m1.emplace_back("short");
        m1.emplace_back(longStr);
        REQUIRE(m1[0] == longStr);
        auto  sz  = m1.size();
        auto  cp  = m1.capacity();
        auto* beg = m1.data();
        // NOLINTBEGIN(bugprone-use-after-move)
        SECTION("move construct") {
            StrArray m2(std::move(m1));
            CHECK(m1.capacity() == 0);
            CHECK(m1.data() == nullptr);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() == cp);
            CHECK(beg == m2.data());
        }

        SECTION("copy construct") {
            StrArray m2(m1);
            CHECK(m2.size() == sz);
            CHECK(m1.size() == sz);
            CHECK(m1.capacity() == cp);
            CHECK(m2.capacity() <= cp);
            CHECK(m1.data() == beg);
            CHECK(m2.data() != beg);
            CHECK(longStr == m2[0]);
            CHECK(m1[0] == m2[0]);
            CHECK(m1[0].data() != m2[0].data());

            CHECK(m1 == m2);
        }

        SECTION("move assign") {
            StrArray m2;
            m2.push_back(ConstString("Foo"));
            m2.push_back(ConstString("Bar"));
            m2 = std::move(m1);
            CHECK(m1.empty());
            CHECK(m1.data() == nullptr);
            CHECK(m2.data() == beg);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() == cp);
            CHECK(longStr == m2[0]);
        }
        // NOLINTEND(bugprone-use-after-move)
        SECTION("copy assign") {
            StrArray m2;
            m2.push_back(ConstString("Foo"));
            m2.push_back(ConstString("Bar"));
            m2 = m1;
            CHECK(m1.size() == sz);
            CHECK(m1.data() == beg);
            CHECK(m2.data() != beg);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() <= cp);
            CHECK(m1 == m2);
            CHECK(m1[0].data() != m2[0].data());
        }
    }
}
TEST_CASE("Test HashMap", "[util]") {
    using MapT = SimpleHashMap<int, double, INT32_MAX>;
    SECTION("Init") {
        using BuckT  = std::optional<unsigned>;
        auto buckets = GENERATE(BuckT{}, BuckT{0u}, BuckT{3u});
        MapT m       = buckets.has_value() ? MapT{*buckets} : MapT{};
        CAPTURE(buckets);
        REQUIRE(m.empty());
        REQUIRE(m.size() == 0u);
        REQUIRE_FALSE(m.contains(0));
        REQUIRE(m.findKey(0) == nullptr);
        REQUIRE_FALSE(m.remove(0));
        REQUIRE(m.array().size() >= buckets.value_or(0u));
        REQUIRE(std::ranges::all_of(m.array(), [](const auto& x) { return x.key == INT32_MAX; }));
    }
    SECTION("add") {
        MapT m;
        REQUIRE(m.add(0, 17.0).second);
        REQUIRE_FALSE(m.add(0).second);
        REQUIRE(m.add(0, 22.0).first->value == 17.0);
        REQUIRE(m.size() == 1);
        REQUIRE(m.contains(0));
        REQUIRE(m.findKey(0)->value == 17.0);
        REQUIRE(m.findKey(0)->key == 0);
        REQUIRE(m.findKey(0)->value == 17.0);

        m.add(0).first->value = 99.0;
        REQUIRE(m.findKey(0)->key == 0);
        REQUIRE(m.findKey(0)->value == 99.0);
    }
    SECTION("copy and move") {
        MapT source, saved;
        for (auto i : std::ranges::views::iota(0, 10)) {
            REQUIRE(source.add(i, i * 1.0).second);
            saved.add(i, i * 1.0);
            REQUIRE(source.contains(i));
        }
        REQUIRE(source.size() == 10);
        REQUIRE(source.array().size() > 10u);
        REQUIRE(saved.size() == source.size());

        REQUIRE(source.remove(8));
        REQUIRE(source.remove(2));
        REQUIRE(source.remove(1));
        REQUIRE(source.remove(9));
        REQUIRE(source.remove(4));
        REQUIRE(source.size() == 5u);

        SECTION("copy") {
            MapT copy(source);
            REQUIRE(copy.size() == source.size());
            REQUIRE(copy.array().data() != source.array().data());
            REQUIRE(copy.array().size() < source.array().size());
            for (auto i : std::ranges::views::iota(0, 10)) {
                CAPTURE(i);
                REQUIRE(source.contains(i) == copy.contains(i));
                REQUIRE(*source.add(i, 99.0).first == *copy.add(i, 99.0).first);
            }
            copy = saved;
            REQUIRE(copy.size() == saved.size());
            REQUIRE(copy.array().data() != saved.array().data());
            for (auto i : std::ranges::views::iota(0, 10)) {
                REQUIRE(saved.contains(i));
                REQUIRE(saved.contains(i) == copy.contains(i));
                REQUIRE(*saved.findKey(i) == *copy.findKey(i));
            }
            REQUIRE(std::ranges::equal(saved.array(), copy.array(), [](const auto& lhs, const auto& rhs) {
                return lhs.key == rhs.key && lhs.value == rhs.value;
            }));
            copy = {};
            REQUIRE(copy.empty());
            REQUIRE(copy.array().empty());
            source.clear();
            REQUIRE(source.empty());
            REQUIRE_FALSE(source.array().empty());
            REQUIRE(std::ranges::all_of(source.array(), [](const auto& x) { return x.key == INT32_MAX; }));
        }
        SECTION("move") {
            auto prev = source.array();
            MapT mv(std::move(source));
            REQUIRE(mv.size() == 5u);
            REQUIRE(source.empty());
            REQUIRE(mv.array().data() == prev.data());

            prev = saved.array();
            mv   = std::move(saved);
            REQUIRE(mv.size() == 10u);
            REQUIRE(saved.empty());
            REQUIRE(mv.array().data() == prev.data());
        }
    }
    SECTION("erase") {
        SimpleHashMap<int, int, INT32_MAX> m;

        auto rem = std::vector{1, 9, 17, 25};
        do {
            REQUIRE(m.add(1).first);
            REQUIRE(m.add(9).first);
            REQUIRE(m.add(17).first);
            REQUIRE(m.add(25).first);
            REQUIRE(m.size() == 4u);

            REQUIRE(m.remove(rem.back()));
            REQUIRE_FALSE(m.contains(rem.back()));
            for (auto o : std::span(rem).first(rem.size() - 1)) {
                //
                REQUIRE(m.contains(o));
            }
            m.clear();
        } while (std::ranges::next_permutation(rem).found);
    }
}
TEST_CASE("Test HashIndex", "[util]") {
    static_assert(std::is_move_constructible_v<DynamicIndex>, "should be movable");
    static_assert(std::is_move_assignable_v<DynamicIndex>, "should be movable");
    static_assert(std::is_copy_assignable_v<DynamicIndex>, "should be copyable");
    static_assert(std::is_copy_constructible_v<DynamicIndex>, "should be copyable");
    static_assert(DynamicIndex::trivially_relocatable::value, "should be trivially relocatable");

    SECTION("empty") {
        DynamicIndex index;
        REQUIRE(index.size() == 0u); // NOLINT
        REQUIRE(index.empty());
        REQUIRE(index.buckets() == 0u);
        REQUIRE(index.full());
        REQUIRE_FALSE(index.contains(0u, 0u));
    }
    SECTION("supports initial capacity") {
        auto x = GENERATE(3u, 16u, 32u, 256u, 1024u);
        for (auto y : {x - 2, x, x + 1}) {
            CAPTURE(x);
            CAPTURE(y);
            DynamicIndex index(y);
            REQUIRE(index.buckets() == std::max(Potassco::bit_ceil(y), 8u));
            REQUIRE(index.empty());
        }
        DynamicIndex index(0);
        REQUIRE(index.buckets() == 0u);
    }
    SECTION("try_add") {
        DynamicIndex index;
        REQUIRE(index.try_add(0, 0));

        REQUIRE(index.size() == 1u);
        REQUIRE_FALSE(index.empty());
        REQUIRE(index.contains(0, 0));
        REQUIRE(index.find_if(0, 0).valid());

        REQUIRE_FALSE(index.try_add(0, 0));
    }

    SECTION("add hint") {
        DynamicIndex index;
        auto         r = index.find_if(4711, 0);
        REQUIRE_FALSE(r);
        index.add(r, 4711, 0);
        REQUIRE(index.find_if(4711, 0).valid());
        REQUIRE_FALSE(index.try_add(4711, 0));
    }

    SECTION("add collision") {
        DynamicIndex index(0, 0.5);
        for (unsigned i = 0; i != 4; ++i) {
            REQUIRE(index.try_add(0, i));
            REQUIRE(index.find_if(0, i).valid());
        }
        REQUIRE(index.buckets() == 8u);
        REQUIRE(index.size() == 4u);
        REQUIRE(index.full());
        SECTION("grow") {
            REQUIRE(index.try_add(4711, 32u));
            REQUIRE(index.size() == 5u);
            REQUIRE(index.buckets() == 16u);
            for (unsigned i = 0; i != 4; ++i) { REQUIRE_FALSE(index.try_add(0, i)); }
        }
    }

    SECTION("add bucket collision") {
        DynamicIndex index(0, 0.5);
        REQUIRE(index.try_add(0u, 1u));
        REQUIRE(index.try_add(8u, 2u));
        REQUIRE(index.try_add(16u, 4u));
        REQUIRE(index.try_add(0u, 5u));
        REQUIRE(index.buckets() == 8u);
        REQUIRE(index.size() == 4u);

        bool ok = true;
        REQUIRE(index.contains(0, [&ok](uint32_t id) {
            ok = ok && test_bit(id, 0);
            return id == 5u;
        }));
        REQUIRE(index.contains(0, [&ok](uint32_t id) {
            ok = ok && test_bit(id, 0);
            return id == 1u;
        }));
        REQUIRE(ok);
    }
    SECTION("find fails on wrong hash") {
        DynamicIndex index;
        REQUIRE(index.try_add(0u, 1u));
        REQUIRE(index.contains(0u, 1u));
        REQUIRE_FALSE(index.contains(4711u, 1u));
    }
    SECTION("full") {
        DynamicIndex index(1, 0.99);
        for (auto i = 0u; not index.full();) { REQUIRE(index.try_add(0u, i++)); }
        REQUIRE(index.buckets() == 8u);
        REQUIRE(index.size() == 7u);
        REQUIRE(index.contains(0u, 1u));
        REQUIRE_FALSE(index.contains(0u, 8u));
    }
    SECTION("copy and move") {
        DynamicIndex index(64u);
        for (unsigned i = 0; i != 10; ++i) { index.try_add(i, i); }
        REQUIRE(index.size() == 10u);
        REQUIRE(index.buckets() == 64u);
        SECTION("copy") {
            DynamicIndex rhs(index);
            REQUIRE(rhs.buckets() == 16u);
            REQUIRE(rhs.size() == 10u);
            for (unsigned i = 0; i != 10; ++i) {
                REQUIRE(index.contains(i, i));
                REQUIRE(rhs.contains(i, i));
            }
            DynamicIndex empty;
            DynamicIndex alsoEmpty(empty);
            REQUIRE(empty.size() == alsoEmpty.size());
            REQUIRE(empty.buckets() == alsoEmpty.buckets());
        }
        SECTION("move") {
            DynamicIndex rhs(std::move(index));
            REQUIRE(index.empty()); // NOLINT
            REQUIRE(index.full());
            REQUIRE(index.buckets() == 0u);

            REQUIRE(rhs.buckets() == 64u);
            REQUIRE(rhs.size() == 10u);
            for (unsigned i = 0; i != 10; ++i) { REQUIRE(rhs.contains(i, i)); }
        }
        SECTION("assign") {
            DynamicIndex rhs;
            rhs.try_add(20, 20);
            rhs.try_add(21, 21);
            SECTION("copy") {
                rhs = index;
                REQUIRE(rhs.buckets() == 16u);
                REQUIRE(rhs.size() == 10u);
                for (unsigned i = 0; i != 10; ++i) {
                    REQUIRE(index.contains(i, i));
                    REQUIRE(rhs.contains(i, i));
                }
            }
            SECTION("move") {
                rhs = std::move(index);
                REQUIRE(index.empty()); // NOLINT
                REQUIRE(index.full());
                REQUIRE(index.buckets() == 0u);
                REQUIRE(rhs.buckets() == 64u);
                REQUIRE(rhs.size() == 10u);
                for (unsigned i = 0; i != 10; ++i) { REQUIRE(rhs.contains(i, i)); }
            }
            SECTION("self") {
                POTASSCO_WARNING_PUSH()
                POTASSCO_WARNING_IGNORE_CLANG("-Wself-assign-overloaded")
                index = index; // NOLINT
                REQUIRE(index.buckets() == 64u);
                for (unsigned i = 0; i != 10; ++i) { REQUIRE(index.contains(i, i)); }
                POTASSCO_WARNING_IGNORE_GNU("-Wself-move")
                index = std::move(index); // NOLINT
                REQUIRE(index.buckets() == 64u);
                for (unsigned i = 0; i != 10; ++i) { REQUIRE(index.contains(i, i)); }
                POTASSCO_WARNING_POP()
            }
        }
    }
    SECTION("erase") {
        DynamicIndex index(0, 0.7);
        REQUIRE(index.try_add(0, 0));
        REQUIRE(index.erase(index.find_if(0, 0)));
        REQUIRE(index.empty());
        REQUIRE_FALSE(index.erase(index.find_if(0, 0)));

        SECTION("reuse") {
            REQUIRE(index.try_add(0u, 1u));
            REQUIRE(index.try_add(0u, 2u));
            REQUIRE(index.try_add(0u, 3u));
            auto        r = index.find_if(0u, 2u);
            const auto* b = r.bucket();
            REQUIRE(r);
            REQUIRE(index.erase(r));

            REQUIRE(index.contains(0u, 1u));
            REQUIRE(index.contains(0u, 3u));
            r = index.find_if(0u, 2u);
            REQUIRE_FALSE(r);
            REQUIRE(r.bucket() == b);
        }
        SECTION("consolidate") {
            auto hash = [](uint32_t i) { return static_cast<uint32_t>(i * 11111111111u); };
            for (unsigned i = 0; index.buckets() < 16 || not index.full(); ++i) {
                //
                REQUIRE(index.try_add(hash(i), i));
            }
            REQUIRE(index.buckets() == 16u);
            REQUIRE(index.size() == 11u);

            REQUIRE(index.erase(index.find_if(hash(0), 0)));
            REQUIRE(index.full());
            REQUIRE(index.size() == 10u);

            REQUIRE(index.erase(index.find_if(hash(2), 2)));
            REQUIRE(index.full());
            REQUIRE(index.size() == 9u);

            REQUIRE(index.erase(index.find_if(hash(6), 6)));
            REQUIRE(index.full());
            REQUIRE(index.size() == 8u);

            REQUIRE(index.erase(index.find_if(hash(1), 1)));
            REQUIRE(index.full());
            REQUIRE(index.size() == 7u);

            REQUIRE(index.erase(index.find_if(hash(10), 10)));
            REQUIRE(index.full());
            REQUIRE(index.size() == 6u);
            REQUIRE(index.buckets() == 16u);

            for (auto i : {3u, 4u, 5u, 7u, 8u, 9u}) { REQUIRE(index.contains(hash(i), i)); }
            REQUIRE(index.erase(index.find_if(hash(5), 5)));
            REQUIRE(index.size() == 5u);
            REQUIRE_FALSE(index.full());

            for (auto i : {3u, 4u, 7u, 8u, 9u}) { REQUIRE(index.erase(index.find_if(hash(i), i))); }
        }
        SECTION("consolidatePermute") {
            index      = DynamicIndex(0u, 0.99);
            auto elems = std::vector<std::pair<uint32_t, uint32_t>>{{2, 'a'}, {2, 'b'}, {3, 'x'}, {4, 'c'},
                                                                    {5, 'd'}, {6, 'e'}, {7, 'f'}};

            SECTION("moveRight") {
                // F F a b c d e f-> x
                for (auto [hash, val] : elems) {
                    if (val != 'x') {
                        REQUIRE(index.try_add(hash, val));
                    }
                }
                // x F a b c d e f
                REQUIRE(index.try_add(3, 'x'));
                REQUIRE(index.buckets() == 8u);
                REQUIRE(index.size() == 7u);
                REQUIRE(index.full());
                auto* xSlot = index.find_if(3, 'x').bucket();

                // x F a b c d e f -> erase
                // x F a b c T T T
                for (auto [hash, val] : std::span(elems).last(3)) { REQUIRE(index.erase(index.find_if(hash, val))); }
                REQUIRE(index.full());

                // x F a b T T T T -> consolidate
                REQUIRE(index.erase(index.find_if(elems[3].first, elems[3].second)));
                REQUIRE(index.size() == 3u);
                REQUIRE_FALSE(index.full());

                // F F a b x F F F
                REQUIRE(index.find_if(3, 'x').bucket() > xSlot);
            }
            REQUIRE(std::ranges::is_sorted(elems));
            do {
                CAPTURE(elems);
                index.clear();
                for (auto [hash, val] : elems) { REQUIRE(index.try_add(hash, val)); }
                REQUIRE(index.buckets() == 8u);
                REQUIRE(index.size() == 7u);
                REQUIRE(index.full());

                for (auto [hash, val] : std::span(elems).last(3)) { REQUIRE(index.erase(index.find_if(hash, val))); }
                REQUIRE(index.full());
                REQUIRE(index.erase(index.find_if(elems[3].first, elems[3].second)));
                REQUIRE(index.size() == 3u);
                REQUIRE_FALSE(index.full());
                for (auto [hash, val] : std::span(elems).first(3)) { REQUIRE(index.find_if(hash, val)); }
                for (auto [hash, val] : std::span(elems).last(4)) { REQUIRE_FALSE(index.find_if(hash, val)); }
            } while (std::ranges::next_permutation(elems).found);
        }
    }
    SECTION("clear") {
        DynamicIndex index;
        index.try_add(0u, 0u);
        index.try_add(10u, 10u);
        REQUIRE(index.buckets() == 8u);
        index.clear();
        REQUIRE(index.empty());
        REQUIRE(index.size() == 0u); // NOLINT
        REQUIRE_FALSE(index.full());
        REQUIRE(index.buckets() == 8u);
        REQUIRE_FALSE(index.contains(0u, 0u));
        REQUIRE_FALSE(index.contains(10u, 10u));

        index.try_add(0u, 0u);
        REQUIRE(index.contains(0u, 0u));
        REQUIRE_FALSE(index.contains(10u, 10u));

        index.discard();
        REQUIRE(index.empty());
        REQUIRE(index.full());
        REQUIRE(index.buckets() == 0u);
    }
}
TEST_CASE("Test ConstString", "[util]") {
    SECTION("empty") {
        ConstString s;
        REQUIRE(s.size() == 0);
        REQUIRE(s.c_str() != nullptr);
        REQUIRE(s.data() != nullptr);
        REQUIRE(s.view() == std::string_view{});
        REQUIRE_FALSE(*s.c_str());
    }
    SECTION("small to large") {
        for (unsigned i = 0;; ++i) {
            std::string v(i, 'x');
            ConstString s(v);
            CAPTURE(i);
            REQUIRE(s.size() == v.size());
            REQUIRE(s[s.size()] == 0);
            REQUIRE(std::strcmp(s.c_str(), v.c_str()) == 0);
            REQUIRE(s.view() == v);
            if (not s.small()) {
                REQUIRE(v.size() == 16);
                break;
            }
        }
    }
    SECTION("deep copy") {
        std::string_view sv("small");
        ConstString      s(sv);
        ConstString      s2(s);
        REQUIRE(s == sv);
        REQUIRE(s2 == sv);
        REQUIRE(sv.data() != s.data());
        REQUIRE(s.data() != s2.data());
        std::string large(32, 'x');
        ConstString s3(large);
        ConstString s4(s3);
        REQUIRE(s3 == std::string_view{large});
        REQUIRE(s4 == std::string_view{large});
        REQUIRE(s3.data() != s4.data());

        SECTION("assign") {
            ConstString sc;
            sc = s3;
            REQUIRE(sc == std::string_view{large});
            REQUIRE(sc.data() != s3.data());
            sc = s2;
            REQUIRE(sc == sv);
            REQUIRE(sc.data() != s2.data());
        }
        SECTION("self assign") {
            const void* old = s3.data();
            POTASSCO_WARNING_PUSH()
            POTASSCO_WARNING_IGNORE_CLANG("-Wself-assign-overloaded")
            s3 = s3; // NOLINT
            POTASSCO_WARNING_POP()
            REQUIRE(old == s3.data());
        }
    }
    SECTION("borrow") {
        std::string_view svSmall("small");
        std::string_view svLarge("long string no sso");
        ConstString      cSmall{ConstString::Borrow_t{}, svSmall};
        ConstString      cLarge{ConstString::Borrow_t{}, svLarge};
        REQUIRE(cSmall.data() == svSmall.data());
        REQUIRE_FALSE(cSmall.small());

        REQUIRE(cLarge.data() == svLarge.data());
        REQUIRE_FALSE(cLarge.small());

        SECTION("materialize on copy") {
            ConstString smallCopy(cSmall); // NOLINT
            ConstString largeCopy(cLarge); // NOLINT
            REQUIRE(smallCopy.small());
            REQUIRE(smallCopy.data() != svSmall.data());

            REQUIRE_FALSE(largeCopy.small());
            REQUIRE(largeCopy.data() != svLarge.data());
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
        REQUIRE(s3 == std::string_view{}); // NOLINT
        REQUIRE(s4 == std::string_view(large));
        REQUIRE(s4.c_str() == old);

        SECTION("assign") {
            s = std::move(s2);
            REQUIRE(s2 == std::string_view{}); // NOLINT
            REQUIRE(s == sv);

            s3 = std::move(s4);
            REQUIRE(s4 == std::string_view{}); // NOLINT
            REQUIRE(s3 == std::string_view(large));
            REQUIRE((const void*) s3.c_str() == old);

            s3 = ConstString();
            REQUIRE(s3 == std::string_view{});
        }
        SECTION("self assign") {
            POTASSCO_WARNING_PUSH()
            POTASSCO_WARNING_IGNORE_CLANG("-Wself-move")
            s4 = static_cast<ConstString&&>(s4); // avoid warning from std::move
            POTASSCO_WARNING_POP()
            REQUIRE((const void*) s4.c_str() == old);
        }
    }
    SECTION("noSSO") {
        ConstString noSso(ConstString::NoSso_t{}, "small");
        REQUIRE_FALSE(noSso.small());
        ConstString copyNoSso(noSso);
        REQUIRE_FALSE(copyNoSso.small());
        ConstString assignNoSso("small");
        REQUIRE(assignNoSso.small());
        REQUIRE(assignNoSso == noSso);
        assignNoSso = noSso;
        REQUIRE_FALSE(assignNoSso.small());
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
    SECTION("try_emplace") {
        using MapT = std::unordered_map<ConstString, int, std::hash<ConstString>, std::equal_to<>>;
        MapT m;
        REQUIRE(try_emplace(m, "foo", 22).second);
        REQUIRE_FALSE(try_emplace(m, "foo", 23).second);
    }
}
TEST_CASE("Test OrderedStringSet", "[util]") {
    static_assert(std::is_move_constructible_v<OrderedStringSet>, "should be movable");
    static_assert(std::is_move_assignable_v<OrderedStringSet>, "should be movable");
    static_assert(not std::is_copy_assignable_v<OrderedStringSet>, "should not be copyable");
    static_assert(not std::is_copy_constructible_v<OrderedStringSet>, "should not be copyable");
    static_assert(DynamicIndex::trivially_relocatable::value, "should be trivially relocatable");

    SECTION("empty") {
        OrderedStringSet set;
        REQUIRE(set.size() == 0u);
        REQUIRE_FALSE(set.contains(""));
        REQUIRE(set.elements().empty());
    }
    SECTION("add") {
        OrderedStringSet set;
        for (auto i : std::ranges::views::iota(0u, 10u)) {
            auto str          = std::string("hello-").append(std::to_string(i));
            auto [idx, added] = set.add(str);
            CAPTURE(i);
            REQUIRE(idx == i);
            REQUIRE(added);
            REQUIRE(set[idx] == str);
            REQUIRE(set.contains(str));
        }
        std::vector exp{std::string_view("hello-0"), std::string_view("hello-1"), std::string_view("hello-2")};
        auto        cmpStr = std::equal_to<>{};
        REQUIRE(std::ranges::equal(set.elements().first(3), exp, cmpStr));

        OrderedStringSet moved(std::move(set));
        REQUIRE(set.size() == 0u);
        REQUIRE(set.elements().empty());
        REQUIRE(moved.size() == 10u);
        REQUIRE(std::ranges::equal(moved.elements().first(3), exp, cmpStr));

        moved.clear();
        REQUIRE(moved.size() == 0u);
        REQUIRE(moved.elements().empty());
    }
    SECTION("SSO") {
        OrderedStringSet sso, noSso(false);
        for (auto i : std::ranges::views::iota(0u, 16u)) {
            auto str            = std::string(i == 0u ? 16u : i, 'x');
            auto [idx1, added1] = sso.add(str);
            auto [idx2, added2] = noSso.add(str);
            CAPTURE(i);
            REQUIRE(idx1 == i);
            REQUIRE(added1);
            REQUIRE(idx2 == i);
            REQUIRE(added2);
            REQUIRE(sso[idx1] == str);
            REQUIRE(noSso[idx2] == str);
            REQUIRE(sso.contains(str));
            REQUIRE(noSso.contains(str));

            REQUIRE(sso[i].small() == (i != 0));
            REQUIRE_FALSE(noSso[i].small());
        }
    }
}
TEST_CASE("Test RadixSort", "[util]") {
    SECTION("trivial") {
        auto v = GENERATE(std::vector<unsigned>{}, std::vector({1u}));
        auto e = v;
        struct NotUsed {
            void resize(std::size_t, unsigned) { FAIL("must not be called"); } // NOLINT
            auto data() -> unsigned* { FAIL("must not be called"); }           // NOLINT
        } tmp;
        CAPTURE(v);
        radixSort(v, std::identity{}, radix_def, tmp);
        REQUIRE(v == e);
    }
    SECTION("small") {
        auto v = std::vector<unsigned>{47, 86, 22, 93, 102, 1, 28, 17, 100};
        auto e = v;
        struct NotUsed {
            void resize(std::size_t, unsigned) { FAIL("must not be called"); } // NOLINT
            auto data() -> unsigned* { FAIL("must not be called"); }           // NOLINT
        } tmp;
        CAPTURE(v);
        radixSort(v, std::identity{}, radix_def, tmp);
        std::ranges::sort(e);
        REQUIRE(v == e);
    }
    SECTION("sorted") {
        auto v = GENERATE(std::vector{1u, 2u, 3u, 4u, 5u, 6u, 7u},
                          std::vector{10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u});
        auto e = v;
        CAPTURE(v);
        radixSort(v, std::identity{}, radix_only);
        REQUIRE(v == e);
        std::vector<unsigned> buf;
        radixSort(v, std::identity{}, radix_only, std::ref(buf));
        REQUIRE(v == e);
        if (e.back() < 10u) {
            REQUIRE(buf.empty());
        }
        else {
            REQUIRE(buf.size() == v.size());
        }
    }
    SECTION("random") {
        std::vector<unsigned> v;
        std::mt19937          rng(47110815);
        auto                  size = GENERATE(10u, 50u, 100u, 1000u);
        for (auto i = 0u; i != size; ++i) { v.push_back(rng()); }
        auto e = v;
        std::ranges::sort(e);
        radixSort(v, std::identity{}, radix_only);
        CAPTURE(size);
        REQUIRE(v == e);
    }
    SECTION("domain") {
        std::vector<uint64_t> v;
        for (unsigned i = 1; i < 64; ++i) {
            auto x = Potassco::bit_max<uint64_t>(i);
            v.push_back(x + 1);
            v.push_back(x);
            v.push_back(x - 1);
        }
        std::mt19937 g(47110815);
        auto         e = v;
        std::ranges::sort(e);
        for (auto i = 0; i != 10; ++i) {
            CAPTURE(i);
            std::ranges::shuffle(v, g);
            radixSort(v, std::identity{}, radix_only);
            REQUIRE(v == e);
        }
    }
    SECTION("rankFun") {
        using D = std::pair<std::string, unsigned>;
        auto v  = std::vector<D>({{"I", 1u},
                                  {"II", 2u},
                                  {"V", 5u},
                                  {"X", 10u},
                                  {"XL", 40u},
                                  {"L", 50u},
                                  {"D", 500u},
                                  {"M", 1000u},
                                  {"large", 2151677984u}});
        auto e  = v;
        std::ranges::sort(e, std::less{}, [](const D& d) { return d.second; });
        std::mt19937 g(47110815);
        for (auto i = 0; i != 10; ++i) {
            CAPTURE(i);
            std::ranges::shuffle(v, g);
            radixSort(v, [](const D& d) { return d.second; }, radix_only);
            REQUIRE(v == e);
        }
    }
    SECTION("stable") {
        auto r = std::map<unsigned, unsigned>{
            {75, 35722},  {262, 35548}, {289, 35550}, {150, 36466}, {151, 35532}, {80, 35530}, {112, 35567},
            {61, 36414},  {51, 35588},  {49, 36425},  {105, 36513}, {196, 35559}, {20, 35570}, {233, 35567},
            {109, 35531}, {21, 35529},  {232, 35540}, {17, 35566},  {170, 35565}, {54, 35537}, {56, 35549},
            {245, 35547}, {282, 35568}, {143, 35546}, {159, 35545}, {85, 35532},  {74, 35554}, {156, 35557},
            {148, 35579}, {250, 35553}, {15, 35552},
        };
        auto v = std::vector<unsigned>{
            75,  262, 289, 150, 151, 80,  112, 61,  51,  49, 105, 196, 20,  233, 109, 21,
            232, 17,  170, 54,  56,  245, 282, 143, 159, 85, 74,  156, 148, 250, 15,
        };
        auto c = v;
        radixSort(v, [&](unsigned x) { return r.at(x); }, radix_def);
        radixSort(c, [&](unsigned x) { return r.at(x); }, radix_only);
        REQUIRE(v == c);
    }
    SECTION("tmpBuf") {
        Detail::Temp<int> x;
        REQUIRE(x.data() == nullptr);
        x.resize(10, 3);
        REQUIRE(x.data() != nullptr);
        REQUIRE(*x.data() == 3);
        struct NoDef {
            explicit NoDef(int x) : i(x) {}
            int i;
        };
        Detail::Temp<NoDef> y;
        REQUIRE(y.data() == nullptr);
        y.resize(10, NoDef{9});
        REQUIRE(y.data() != nullptr);
        REQUIRE(y.data()->i == 9);
        y.resize(12, NoDef{19});
        REQUIRE(y.data()->i == 19);
        Detail::Temp<std::string> z;
        z.resize(10, "Bla");
        REQUIRE(*z.data() == "Bla"s);
        z.data()[4] = std::string(256, 'x');
        REQUIRE(z.data()[4] == std::string(256, 'x'));
        z.resize(8, "Foo");
        REQUIRE(z.data()[4] == "Foo"s);
    }
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

    SECTION("dynamic bitset") {
        DynamicBitset bitset;
        CHECK(bitset.count() == 0);
        CHECK(bitset.words() == 0);
        CHECK(bitset.smallest() == 0);
        CHECK(bitset.largest() == 0);
        CHECK(bitset == bitset);
        CHECK_FALSE(bitset < bitset);
        CHECK_FALSE(bitset > bitset);
        CHECK(bitset <= bitset);

        bitset.add(63);
        CHECK(bitset.count() == 1);
        CHECK(bitset.words() == 1);
        CHECK(bitset.smallest() == 63);
        CHECK(bitset.largest() == 63);
        CHECK(bitset.contains(63));
        CHECK_FALSE(bitset.contains(64));
        DynamicBitset other;
        CHECK(other < bitset);
        bitset.add(64);
        CHECK(bitset.count() == 2);
        CHECK(bitset.smallest() == 63);
        CHECK(bitset.contains(64));
        bitset.add(12);
        CHECK(bitset.smallest() == 12);
        CHECK(bitset.largest() == 64);
        bitset.remove(12);

        other.add(64);
        CHECK(other < bitset);
        other.add(65);
        CHECK(other > bitset);
        CHECK(other.contains(65));
        CHECK(other.largest() == 65);
        CHECK(other.words() == 2);
        other.add(128);
        CHECK(other.largest() == 128);
        CHECK(other > bitset);
        CHECK(other.count() == 3);
        other.remove(65);
        CHECK(other > bitset);
        other.add(63);
        other.remove(128);
        CHECK(other == bitset);
        other.add(4096);
        CHECK(other.largest() == 4096);
        other.add(100000);
        CHECK(other.largest() == 100000);
        CHECK(other.count() == 4);
        other.remove(100000);
        CHECK(other.count() == 3);
        CHECK(other.words() == 65);
        CHECK(other.largest() == 4096);

        other.add(0);
        other.add(1);
        other.add(65);
        other.add(128);
        other.add(129);
        other.add(130);
        CHECK(other.count() == 9);
        other.apply(~static_cast<uint64_t>(3u));
        CHECK_FALSE(other.contains(0));
        CHECK_FALSE(other.contains(1));
        CHECK_FALSE(other.contains(64));
        CHECK_FALSE(other.contains(65));
        CHECK_FALSE(other.contains(128));
        CHECK_FALSE(other.contains(129));

        CHECK(other.contains(63));
        CHECK(other.contains(130));
        CHECK(other.count() == 2);
        CHECK(other.largest() == 130);
        CHECK(other.words() == 3);
    }

    SECTION("cmpAtom") {
        CHECK(std::is_eq(cmpAtom("", "", {})));
        CHECK(std::is_lt(cmpAtom("a", "b", {})));
        CHECK(std::is_lt(cmpAtom("10", "2", {})));
        CHECK(std::is_gt(cmpAtom("2", "10", {})));
        CHECK(std::is_gt(cmpAtom("b", "a", {})));
    }

    SECTION("cmpAtomNatural") {
        auto cmp = AtomCompare::cmp_natural;
        CHECK(std::is_eq(cmpAtom("", "", cmp)));
        CHECK(std::is_lt(cmpAtom("a", "b", cmp)));
        CHECK(std::is_gt(cmpAtom("b", "a", cmp)));

        CHECK(std::is_gt(cmpAtom("10", "2", cmp)));
        CHECK(std::is_lt(cmpAtom("2", "10", cmp)));
        CHECK(std::is_eq(cmpAtom("0", "0", cmp)));

        CHECK(std::is_gt(cmpAtom("a(10)", "a(2)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(2,10,4)", "a(2,10,10)", cmp)));
        CHECK(std::is_eq(cmpAtom("a(2,10,4)", "a(2,10,4)", cmp)));
        CHECK(std::is_gt(cmpAtom("a(2,10,8)", "a(2,10,4)", cmp)));

        CHECK(std::is_lt(cmpAtom("a(0)", "a(2)", cmp)));
        CHECK(std::is_eq(cmpAtom("a(0)", "a(0)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(001)", "a(2)", cmp)));

        CHECK(std::is_eq(cmpAtom("a(00001)", "a(01)", cmp)));
        CHECK(std::is_eq(cmpAtom("a(01)", "a(000001)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(01)", "a(000002)", cmp)));
        CHECK(std::is_gt(cmpAtom("a(02)", "a(000001)", cmp)));

        CHECK(std::is_gt(cmpAtom("a(184467440737095516150)", "a(2)", cmp)));
        CHECK(std::is_eq(cmpAtom("a(184467440737095516150)", "a(184467440737095516150)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(2)", "a(184467440737095516150)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(-12)", "a(-11)", cmp)));
    }

    SECTION("cmpAtomArity") {
        auto cmp = AtomCompare::cmp_arity;
        CHECK(std::is_eq(cmpAtom("", "", cmp)));
        CHECK(std::is_lt(cmpAtom("a", "b", cmp)));
        CHECK(std::is_gt(cmpAtom("b", "a", cmp)));

        CHECK(std::is_lt(cmpAtom("a(2)", "a(1,0)", cmp)));
        CHECK(std::is_lt(cmpAtom("b(2)", "a(1,0)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(1,0)", "x(1,0)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(10,0)", "a(2,0)", cmp)));
    }

    SECTION("cmpAtomNaturalArity") {
        auto cmp = AtomCompare::cmp_arity | AtomCompare::cmp_natural;
        CHECK(std::is_eq(cmpAtom("", "", cmp)));
        CHECK(std::is_lt(cmpAtom("a", "b", cmp)));
        CHECK(std::is_gt(cmpAtom("b", "a", cmp)));

        CHECK(std::is_lt(cmpAtom("a(2)", "a(1,0)", cmp)));
        CHECK(std::is_lt(cmpAtom("b(2)", "a(1,0)", cmp)));
        CHECK(std::is_gt(cmpAtom("a(10,0)", "a(2,0)", cmp)));
        CHECK(std::is_lt(cmpAtom("a(2,0)", "a(10,0)", cmp)));
    }

    SECTION("predicate") {
        CHECK(predicate({}) == std::pair(""sv, 0));
        CHECK(predicate("foo"sv) == std::pair("foo"sv, 0));
        CHECK(predicate("foo(x)"sv) == std::pair("foo"sv, 1));
        CHECK(predicate("foo(x, y)"sv) == std::pair("foo"sv, 2));
        CHECK(predicate("foo(\"bla\")"sv) == std::pair("foo"sv, 1));
        CHECK(predicate("tuple((1,2),(3,4))"sv) == std::pair("tuple"sv, 2));
        CHECK(predicate("(1,2)"sv) == std::pair(""sv, 2));
        CHECK(predicate("1,2"sv) == std::pair("1,2"sv, 0));
        // invalid
        CHECK(predicate("tuple((1,2),(3,4)"sv) == std::pair("tuple"sv, -1));
        CHECK(predicate("foo(\"bla)"sv) == std::pair("foo"sv, -1));
    }
    SECTION("atomSymbol") {
        using AtomView = std::tuple<std::string_view, int, std::string_view>;
        CHECK(atomSymbol("foo"sv) == AtomView{"foo"sv, 0, ""sv});
        CHECK(atomSymbol("foo(x)"sv) == AtomView("foo"sv, 1, "x"sv));
        CHECK(atomSymbol("foo(x,y)"sv) == AtomView("foo"sv, 2, "x,y"sv));
        CHECK(atomSymbol("foo(\"bla\",2)"sv) == AtomView("foo"sv, 2, "\"bla\",2"sv));
        CHECK(atomSymbol("tuple((1,2),(3,4))"sv) == AtomView("tuple"sv, 2, "(1,2),(3,4)"sv));
        CHECK(atomSymbol("(1,2,3)"sv) == AtomView(""sv, 3, "1,2,3"sv));
        // invalid
        CHECK(atomSymbol({}) == AtomView(""sv, 0, ""sv));
        CHECK(atomSymbol("tuple((1,2),(3,4)"sv) == AtomView("tuple"sv, -1, ""sv));
    }
    SECTION("popArg") {
        auto pop = [](std::string_view in, AtomArg pos = AtomArg::first, AtomArgMode m = AtomArgMode::raw) {
            return std::pair{popArg(in, pos, m), in};
        };
        CHECK(pop("1,2,3"sv) == std::pair("1"sv, "2,3"sv));
        CHECK(pop("1,2,3"sv, AtomArg::last) == std::pair("3"sv, "1,2"sv));
        CHECK(pop("0"sv, AtomArg::last) == std::pair("0"sv, ""sv));
        CHECK(pop("(1,2),\"(3,4)\""sv, AtomArg::last, AtomArgMode::unquote) == std::pair("(3,4)"sv, "(1,2)"sv));
        CHECK(pop("\"(1,2)\",\"(3,4)\""sv, AtomArg::first, AtomArgMode::unquote) ==
              std::pair("(1,2)"sv, "\"(3,4)\""sv));

        CHECK(pop("\"1,2\",3") == std::pair("\"1,2\""sv, "3"sv));
        CHECK(pop("1,\"2,3\"", AtomArg::last) == std::pair("\"2,3\""sv, "1"sv));

        CHECK(pop("(\"1,2\",3),4") == std::pair("(\"1,2\",3)"sv, "4"sv));
        CHECK(pop("1,(\"1,2\",(3,4))", AtomArg::last) == std::pair("(\"1,2\",(3,4))"sv, "1"sv));

        // Invalid
        CHECK(pop({}) == std::pair(""sv, ""sv));
        CHECK(pop("(1,2,(3,4)"sv) == std::pair("(1,2,(3,4)"sv, ""sv));
        CHECK(pop("(1,2),1,2,3,4)"sv, AtomArg::last) == std::pair("(1,2),1,2,3,4)"sv, ""sv));
    }
    SECTION("enumerate") {
        SECTION("lvalue") {
            std::vector       v{1, 2, 3};
            std::stringstream res;
            for (auto [i, x] : enumerate(v)) {
                res << "(" << i << "," << x << ")";
                ++x;
            }
            REQUIRE(res.str() == "(0,1)(1,2)(2,3)");
            res.str("");
            const auto& cv = v;
            STATIC_CHECK(
                std::is_same_v<decltype(*enumerate(cv).begin()), std::pair<std::vector<int>::size_type, const int&>>);
            for (auto [i, x] : enumerate(cv)) { res << "(" << i << "," << x << ")"; }
            REQUIRE(res.str() == "(0,2)(1,3)(2,4)");
        }
        SECTION("rvalue") {
            std::stringstream res;
            for (auto [i, x] : enumerate(std::vector{1, 2, 3})) {
                ++x;
                res << "(" << i << "," << x << ")";
            }
            REQUIRE(res.str() == "(0,2)(1,3)(2,4)");
        }
    }
}
TEST_CASE("Test RuleBuilder", "[rule]") {
    RuleBuilder rb;
    static_assert(RuleBuilder::trivially_relocatable::value);

    SECTION("fact") {
        rb.start().addHead(1).end();
        REQUIRE(rb.isFact());
    }
    SECTION("simple rule") {
        rb.start().addHead(1).addGoal(2).addGoal(-3).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::normal);
        REQUIRE(spanEq(rb.body(), std::vector{2, -3}));
        REQUIRE_FALSE(rb.isFact());
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
    SECTION("ignore zero weight") {
        rb.start().addHead(1).startSum(2).addGoal(2, 1).addGoal(-3, 0).addGoal(4, 2).end();
        REQUIRE(spanEq(rb.head(), std::vector<Atom_t>{1}));
        REQUIRE(rb.bodyType() == BodyType::sum);
        REQUIRE(rb.bound() == 2);
        REQUIRE(spanEq(rb.sumLits(), std::vector<WeightLit>{{2, 1}, {4, 2}}));
        REQUIRE(spanEq(rb.sum().lits, rb.sumLits()));
        REQUIRE(rb.findSumLit(-3) == nullptr);

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

static void finalize(std::stringstream& is) { is << "0\n"; }
static void finalize(AbstractProgram& prg, std::stringstream& expected) {
    finalize(expected);
    prg.endStep();
}
static int finalize(std::stringstream& in, ReadObserver& observer) {
    finalize(in);
    return readAspif(in, observer);
}
static unsigned compareRead(std::stringstream& input, ReadObserver& observer, std::span<const Rule> rules) {
    for (const auto& r : rules) { toAspif(input, r); }
    finalize(input, observer);
    if (observer.rules.size() != rules.size()) {
        return static_cast<unsigned>(observer.rules.size());
    }
    for (unsigned i = 0; i != rules.size(); ++i) {
        if (rules[i] != observer.rules[i]) {
            return i;
        }
    }
    return static_cast<unsigned>(rules.size());
}

TEST_CASE("Test AspifInput", "[aspif]") {
    std::stringstream input;
    ReadObserver      observer;
    SECTION("basic") {
        input << "asp 1 0 0\n";

        SECTION("read empty") {
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.nStep == 1);
            REQUIRE(observer.incremental == false);
        }

        SECTION("read empty rule") {
            toAspif(input, {HeadType::disjunctive, {}, BodyType::normal, bound_none, {}});
            REQUIRE(finalize(input, observer) == 0);
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
            auto basic   = std::span{rules}.subspan(0, 4);
            auto weight  = std::span{rules}.subspan(4, 2);
            auto mixed   = std::span{rules}.subspan(6, 2);
            auto neg     = std::span{rules}.subspan(8);
            SECTION("simple rules with normal bodies") { REQUIRE(compareRead(input, observer, basic) == basic.size()); }
            SECTION("read rules with weight body") { REQUIRE(compareRead(input, observer, weight) == weight.size()); }
            SECTION("read mixed rules") { REQUIRE(compareRead(input, observer, mixed) == mixed.size()); }
            SECTION("negative weights not allowed in weight rule") {
                REQUIRE_THROWS(compareRead(input, observer, neg));
            }
        }
        SECTION("read minimize rule") {
            input << AspifType::minimize << " -1 3 4 5 6 1 3 2\n";
            input << AspifType::minimize << " 10 3 4 -52 -6 36 3 -20\n";
            REQUIRE(finalize(input, observer) == 0);
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
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.atoms.size() == 1);
            REQUIRE(observer.shows.size() == 1);
            const auto& s1 = observer.shows[0];
            REQUIRE(observer.atoms.at(1) == "a");
            REQUIRE(s1.first == "Hallo Welt");
            REQUIRE(s1.second.at(0) == Vec<Lit_t>({1, -2}));
            REQUIRE(s1.second.size() == 1);
        }
        SECTION("read output with empty condition") {
            SECTION("with fixed fact atom") {
                input << AspifType::output << " 1 a 0\n";
                input << AspifType::output << " 3 foo 0\n";
                finalize(input);
                auto factAtom = GENERATE(15u, 0u);
                CAPTURE(factAtom);
                observer.allowZeroAtom = factAtom == 0;
                AspifInput reader(observer, AspifInput::OutputMapping::atom_fact, factAtom);
                REQUIRE(readProgram(input, reader) == 0);
                REQUIRE(observer.atoms.at(lit(factAtom)) == "a;foo");
            }
            SECTION("without fact") {
                input << AspifType::output << " 1 a 0\n";
                REQUIRE(finalize(input, observer) == 0);
                REQUIRE(observer.atoms.empty());
                REQUIRE(observer.shows.at(0).first == "a");
                REQUIRE(observer.shows.at(0).second.at(0).empty());
                REQUIRE(observer.rules.empty());
            }
            SECTION("with fact") {
                const char* test = GENERATE("before", "after");
                CAPTURE(test);
                if (std::strcmp(test, "before") == 0) {
                    input << AspifType::output << " 1 a 0\n";
                    input << AspifType::rule << " " << HeadType::disjunctive << " 1 1 0 0\n";
                }
                else {
                    input << AspifType::rule << " " << HeadType::disjunctive << " 1 1 0 0\n";
                    input << AspifType::output << " 1 a 0\n";
                }
                REQUIRE(finalize(input, observer) == 0);
                REQUIRE(observer.atoms.size() == 1);
                REQUIRE(observer.atoms.at(1) == "a");
                REQUIRE(observer.shows.empty());
                REQUIRE(observer.rules.size() == 1);
            }
            SECTION("consume facts - round robin") {
                input << AspifType::rule << " " << HeadType::disjunctive << " 1 1 0 0\n";
                input << AspifType::rule << " " << HeadType::disjunctive << " 1 2 0 0\n";
                input << AspifType::rule << " " << HeadType::disjunctive << " 1 3 0 0\n";
                input << AspifType::output << " 1 a 0\n";
                input << AspifType::output << " 1 b 0\n";
                input << AspifType::output << " 1 c 0\n";
                input << AspifType::output << " 1 d 0\n";
                REQUIRE(finalize(input, observer) == 0);
                REQUIRE(observer.rules.size() == 3);
                REQUIRE(observer.atoms.size() == 3);
                REQUIRE(observer.atoms.at(1) == "a;d");
                REQUIRE(observer.atoms.at(2) == "b");
                REQUIRE(observer.atoms.at(3) == "c");
                REQUIRE(observer.shows.empty());
            }
            SECTION("incremental reuses fact") {
                input.str("");
                input << "asp 1 0 0 incremental\n";
                input << AspifType::rule << " " << HeadType::disjunctive << " 1 1 0 0\n";
                input << AspifType::output << " 1 a 0\n";
                finalize(input);
                input << AspifType::rule << " " << HeadType::disjunctive << " 2 2 3 0 0\n";
                input << AspifType::output << " 1 x 0\n";
                REQUIRE(finalize(input, observer) == 0);
                REQUIRE(observer.atoms.size() == 1);
                REQUIRE(observer.atoms.at(1) == "a;x");
                REQUIRE(observer.rules.size() == 2);
                REQUIRE(observer.rules.back().head == Vec<Atom_t>{2, 3});
            }
        }
        SECTION("read projection") {
            input << AspifType::project << " 3 1 2 987232\n";
            input << AspifType::project << " 1 17\n";
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.projects == Vec<Atom_t>({1, 2, 987232, 17}));
        }
        SECTION("read external") {
            std::pair<Atom_t, TruthValue> exp[] = {
                {1, TruthValue::free}, {2, TruthValue::true_}, {3, TruthValue::false_}, {4, TruthValue::release}};
            for (auto&& e : exp) { input << AspifType::external << " " << e.first << " " << e.second << "\n"; }
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(spanEq(observer.externals, std::span{exp, std::size(exp)}));
        }
        SECTION("read assumptions") {
            input << AspifType::assume << " 2 1 987232\n";
            input << AspifType::assume << " 1 -2\n";
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.assumes == Vec<Lit_t>({1, 987232, -2}));
        }
        SECTION("read edges") {
            input << AspifType::edge << " 0 1 2 1 -2\n";
            input << AspifType::edge << " 1 0 1 3\n";
            REQUIRE(finalize(input, observer) == 0);
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
            REQUIRE(finalize(input, observer) == 0);
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
            REQUIRE(finalize(input, observer) == 0);
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
            REQUIRE(finalize(input, observer) == 0);
        }
        SECTION("fails on missing incremental") {
            finalize(input);
            REQUIRE_THROWS(finalize(input, observer));
        }
    }
    SECTION("incremental") {
        input << "asp 1 0 0 incremental\n";
        SECTION("read empty steps") {
            finalize(input);
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.incremental == true);
            REQUIRE(observer.nStep == 2);
        }
        SECTION("read rules in each steps") {
            toAspif(input, {HeadType::disjunctive, {1, 2}, BodyType::normal, bound_none, {}});
            finalize(input);
            toAspif(input, {HeadType::disjunctive, {3, 4}, BodyType::normal, bound_none, {}});
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.incremental == true);
            REQUIRE(observer.nStep == 2);
            REQUIRE(observer.rules.size() == 2);
        }
    }
    SECTION("version 2") {
        input << "asp 2 0 0\n";
        SECTION("output atom") {
            input << AspifType::output << " " << OutputType::atom << " 1 3 foo";
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.atoms.size() == 1);
            REQUIRE(observer.atoms.at(1) == "foo");
        }
        SECTION("invalid output atom") {
            input << AspifType::output << " " << OutputType::atom << " 0 3 foo";
            REQUIRE_THROWS_AS(finalize(input, observer), RuntimeError);
        }
        SECTION("output term") {
            input << AspifType::output << " " << OutputType::term << " 0 3 foo\n";
            input << AspifType::output << " " << OutputType::term << " 1 4 Data\n";
            input << AspifType::output << " " << OutputType::cond << " 1 3 1 -2 3\n";
            input << AspifType::output << " " << OutputType::cond << " 1 2 -2 3\n";
            REQUIRE(finalize(input, observer) == 0);
            REQUIRE(observer.atoms.empty());
            REQUIRE(observer.shows.size() == 2);
            REQUIRE(observer.shows.at(0).first == "foo");
            REQUIRE(observer.shows.at(0).second.empty());
            REQUIRE(observer.shows.at(1).first == "Data");
            REQUIRE(observer.shows.at(1).second.size() == 2);
            REQUIRE(observer.shows.at(1).second[0] == Vec<Lit_t>{1, -2, 3});
            REQUIRE(observer.shows.at(1).second[1] == Vec<Lit_t>{-2, 3});
        }
        SECTION("fails on version 1 output") {
            input << AspifType::output << " 1 a 1 1\n";
            input << AspifType::output << " 10 Hallo Welt 2 1 -2\n";
            REQUIRE_THROWS(finalize(input, observer));
        }
    }
    SECTION("fails on unknown version") {
        input << "asp 1 2 0 incremental\n";
        REQUIRE_THROWS(finalize(input, observer));
    }
    SECTION("fails on unknown tag") {
        input << "asp 1 0 0 foo\n";
        REQUIRE_THROWS(finalize(input, observer));
    }
}
TEST_CASE("Test AspifOutput", "[aspif]") {
    std::stringstream out;
    AspifOutput       writer(out, 1);
    std::stringstream exp;
    exp << "asp 1 0 0\n";
    writer.initProgram(false);
    writer.beginStep();
    auto writeCond = [](std::stringstream& os, const auto& cond) -> std::stringstream& {
        os << cond.size();
        for (auto x : cond) { os << " " << x; }
        return os;
    };
    SECTION("rules") {
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
            toAspif(exp, r);
        }
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("minimize") {
        auto m1 = Vec<WeightLit>{{1, -2}, {-3, 2}, {4, 1}};
        auto m2 = Vec<WeightLit>{{-10, 1}, {-20, 2}};
        writer.minimize(1, m1);
        writer.minimize(-2, m2);
        exp << AspifType::minimize << " 1 3 1 -2 -3 2 4 1\n";
        exp << AspifType::minimize << " -2 2 -10 1 -20 2\n";
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("external") {
        std::pair<Atom_t, TruthValue> ext[] = {
            {1, TruthValue::free}, {2, TruthValue::true_}, {3, TruthValue::false_}, {4, TruthValue::release}};

        for (auto&& e : ext) {
            writer.external(e.first, e.second);
            exp << AspifType::external << " " << e.first << " " << e.second << "\n";
        }
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("output") {
        SECTION("version 1") {
            SECTION("atom") {
                writer.outputAtom(1, "an_atom");
                writer.endStep();
                exp << AspifType::output << " 7 an_atom 1 1\n";
                finalize(exp);
                REQUIRE(exp.str() == out.str());
            }
            SECTION("term conversion") {
                writer.outputTerm(0, "a_term");
                writer.outputTerm(1, "another_term");
                writer.outputTerm(2, "fact");
                auto cond = Vec<Lit_t>{1, 2, 3};
                writer.output(0, cond);
                writer.output(1, cond = Vec<Lit_t>{-1, -2});
                writer.output(2, {});
                finalize(writer, exp);
                REQUIRE("asp 1 0 0\n"
                        "1 0 1 4 0 0\n"             // true.
                        "4 6 a_term 2 5 4\n"        // output a_term : t0, true.
                        "1 0 1 5 0 1 6\n"           // t0 :- aux1.
                        "1 0 1 6 0 3 1 2 3\n"       // aux1 :- {cond}.
                        "4 12 another_term 2 7 4\n" // another_term : t1, true.
                        "1 0 1 7 0 1 8\n"           // t1 :- aux2.
                        "1 0 1 8 0 2 -1 -2\n"       // aux2 :- {cond}.
                        "4 4 fact 2 9 4\n"          // fact : t3, true.
                        "1 0 1 9 0 0\n"             // t3.
                        "0\n" == out.str());
            }
            SECTION("term conversion with rules before after") {
                auto m1 = Vec<WeightLit>{{1, -2}, {-3, 2}, {4, 1}};
                auto m2 = m1;
                m2.push_back(WeightLit{10, 3});
                writer.minimize(1, m1);
                writer.outputTerm(0, "a_term");
                writer.output(0, Vec<Lit_t>{1, 2, 3});
                writer.minimize(-2, m2);
                exp << AspifType::minimize << " 1 3 1 -2 -3 2 4 1\n";
                exp << AspifType::rule << " " << HeadType::disjunctive << " 1 5 0 0\n"; // fact.
                exp << AspifType::output << " 6 a_term 2 6 5\n";
                exp << AspifType::rule << " " << HeadType::disjunctive << " 1 6 0 1 7\n";     // term0 :- aux
                exp << AspifType::rule << " " << HeadType::disjunctive << " 1 7 0 3 1 2 3\n"; // aux :- cond
                exp << AspifType::minimize << " -2 4 1 -2 -3 2 4 1 8 3\n";
                finalize(writer, exp);
                REQUIRE(exp.str() == out.str());
            }
            SECTION("incremental") {
                out.str("");
                writer.initProgram(true);
                writer.beginStep();
                writer.rule(HeadType::choice, Vec<Atom_t>{1, 2, 3}, {});
                writer.outputTerm(0u, "foo");
                writer.output(0u, Vec<Lit_t>{1, -2, 3});
                writer.output(0u, Vec<Lit_t>{-1, -3});

                finalize(writer, exp);
                REQUIRE("asp 1 0 0 incremental\n"
                        "1 1 3 1 2 3 0 0\n"
                        "1 0 1 4 0 0\n"
                        "4 3 foo 2 5 4\n"
                        "1 0 1 5 0 1 6\n"
                        "1 0 1 6 0 3 1 -2 3\n"
                        "1 0 1 5 0 1 7\n"
                        "1 0 1 7 0 2 -1 -3\n"
                        "0\n" == out.str());

                out.str("");
                writer.beginStep();
                writer.rule(HeadType::choice, Vec<Atom_t>{4}, {});
                writer.output(0u, Vec<Lit_t>{3, 4});
                writer.output(0u, Vec<Lit_t>{-3, -4});
                finalize(writer, exp);
                REQUIRE("1 1 1 8 0 0\n"
                        "4 3 foo 2 9 4\n"
                        "1 0 1 9 0 2 10 -5\n"
                        "1 0 1 10 0 2 3 8\n"
                        "1 0 1 9 0 2 11 -5\n"
                        "1 0 1 11 0 2 -3 -8\n"
                        "0\n" == out.str());
            }
            SECTION("incremental 2") {
                out.str("");
                writer.initProgram(true);
                writer.beginStep();

                writer.rule(HeadType::choice, Vec<Atom_t>{1, 2}, {});
                writer.rule(HeadType::disjunctive, Vec<Atom_t>{}, Vec<Lit_t>{-1, -2});
                writer.outputTerm(0u, "a");
                writer.output(0u, Vec<Lit_t>{2});

                finalize(writer, exp);

                writer.beginStep();
                writer.output(0u, Vec<Lit_t>{1});
                finalize(writer, exp);

                REQUIRE("asp 1 0 0 incremental\n"
                        "1 1 2 1 2 0 0\n"   // {x_1;x_2}
                        "1 0 0 0 2 -1 -2\n" // :- not x_1, not x_2
                        "1 0 1 3 0 0\n"     // true.
                        "4 1 a 2 4 3\n"     // #output a : t0, true.
                        "1 0 1 4 0 1 2\n"   // t0 :- x_2.
                        "0\n"
                        "4 1 a 2 5 3\n"      // #output a : t0', x_3
                        "1 0 1 5 0 2 1 -4\n" // t0' :- x_1, not t0.
                        "0\n" == out.str());
            }
        }
        SECTION("version 2") {
            exp.str("");
            out.str("");
            AspifOutput writer2(out);
            exp << "asp 2 0 0\n";
            writer2.initProgram(false);
            writer2.beginStep();
            writer2.outputAtom(1, "an_atom");
            writer2.outputTerm(0, "a_term");
            writer2.outputTerm(1, "another_term");
            auto cond = Vec<Lit_t>{1, 2, 3};
            writer2.output(0, cond);
            writer2.output(1, cond = Vec<Lit_t>{-1, -2});
            exp << AspifType::output << " " << OutputType::atom << " 1 7 an_atom\n";
            exp << AspifType::output << " " << OutputType::term << " 0 6 a_term\n";
            exp << AspifType::output << " " << OutputType::term << " 1 12 another_term\n";
            exp << AspifType::output << " " << OutputType::cond << " 0 3 1 2 3\n";
            exp << AspifType::output << " " << OutputType::cond << " 1 2 -1 -2\n";
            finalize(writer2, exp);
            REQUIRE(exp.str() == out.str());
        }
        SECTION("unsupported zero atom in version 2") {
            AspifOutput writer2(out);
            writer2.initProgram(false);
            writer2.beginStep();
            REQUIRE_THROWS_AS(writer2.outputAtom(0, "fact"), std::logic_error);
        }
    }
    SECTION("assumptions") {
        Lit_t a[] = {1, 987232, -2};
        writer.assume({a, 2});
        writer.assume({a + 2, 1});
        exp << AspifType::assume << " 2 1 987232\n";
        exp << AspifType::assume << " 1 -2\n";
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("projection") {
        Atom_t a[] = {1, 987232, 2};
        writer.project({a, 2});
        writer.project({a + 2, 1});
        exp << AspifType::project << " 2 1 987232\n";
        exp << AspifType::project << " 1 2\n";
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("acyc edges") {
        Edge edge[] = {{0, 1, {1, -2}}, {1, 0, {3}}};
        for (auto&& e : edge) {
            writer.acycEdge(e.s, e.t, e.cond);
            exp << AspifType::edge << " " << e.s << " " << e.t << " ";
            writeCond(exp, e.cond) << "\n";
        }
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
    SECTION("heuristics") {
        Heuristic heu[] = {{1, DomModifier::sign, -1, 1, {10}},
                           {2, DomModifier::level, 10, 3, {-1, 10}},
                           {1, DomModifier::init, 20, 1, {}},
                           {1, DomModifier::factor, 2, 2, {}}};
        for (auto&& h : heu) {
            writer.heuristic(h.atom, h.type, h.bias, h.prio, h.cond);
            exp << AspifType::heuristic << " " << h.type << " " << h.atom << " " << h.bias << " " << h.prio << " ";
            writeCond(exp, h.cond) << "\n";
        }
        finalize(writer, exp);
        REQUIRE(exp.str() == out.str());
    }
}

} // namespace Potassco::Test::Aspif
