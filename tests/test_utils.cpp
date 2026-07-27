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

#include <potassco/utils.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <map>
#include <random>
#include <ranges>

using namespace std::literals;
template <>
struct Catch::StringMaker<std::unique_ptr<int>> {
    static std::string convert(const std::unique_ptr<int>& x) {
        return x ? "unique_ptr<int>=" + std::to_string(*x) : "nullptr";
    }
};
template <typename T>
requires(requires { typename T::stringify_value; })
struct Catch::StringMaker<T> {
    static std::string convert(const T& x) {
        using X = std::remove_cvref_t<decltype(x.value())>;
        return StringMaker<X>{}.convert(x.value());
    }
};
namespace Potassco {
template <typename T, typename U = decltype(std::declval<T>().value()), std::ranges::sized_range R>
requires std::same_as<std::ranges::range_value_t<R>, U>
static auto operator==(const DynamicArray<T>& lhs, const R& rhs) -> bool {
    return std::ranges::equal(lhs, rhs, [](const T& x, const U& y) { return x.value() == y; });
}
template <std::ranges::sized_range R>
requires std::same_as<std::ranges::range_value_t<R>, int>
static auto operator==(const DynamicArray<std::unique_ptr<int>>& lhs, const R& rhs) -> bool {
    return std::ranges::equal(lhs, rhs, [](const auto& x, int y) { return x ? *x == y : y == -1; });
}
template <typename T, std::ranges::sized_range R>
static auto operator==(const DynamicArray<T>& lhs, const R& rhs) -> decltype(std::ranges::equal(lhs, rhs)) {
    return std::ranges::equal(lhs, rhs);
}

} // namespace Potassco
namespace Potassco::Test::Utils {

TEST_CASE("Test Traits", "[util]") {
    STATIC_REQUIRE(TriviallyRelocatable<Atom_t>);
    STATIC_REQUIRE(TriviallyRelocatable<Lit_t>);
    STATIC_REQUIRE(TriviallyRelocatable<Weight_t>);
    STATIC_REQUIRE(TriviallyRelocatable<WeightLit>);
    STATIC_REQUIRE(TriviallyRelocatable<HeadType>);
    STATIC_REQUIRE(TriviallyRelocatable<AtomSpan>);
    STATIC_REQUIRE(TriviallyRelocatable<ConstString>);
    STATIC_REQUIRE(TriviallyRelocatable<std::unique_ptr<Atom_t>>);
    STATIC_REQUIRE(TriviallyRelocatable<std::unique_ptr<std::string>>);
    STATIC_REQUIRE(TriviallyRelocatable<std::pair<Atom_t, Lit_t>>);
    STATIC_REQUIRE(TriviallyRelocatable<DynamicArray<Atom_t>>);

    STATIC_REQUIRE_FALSE(TriviallyRelocatable<std::string>);
    STATIC_REQUIRE_FALSE(TriviallyRelocatable<std::pair<int, std::string>>);
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
    struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__ * 2) HugeAlign {
        int x;
    };
    using StrArray = DynamicArray<ConstString>;
    STATIC_REQUIRE(supportsArray(std::type_identity<Atom_t>{}));
    STATIC_REQUIRE_FALSE(supportsArray(std::type_identity<std::string>{}));
    STATIC_REQUIRE_FALSE(supportsArray(std::type_identity<HugeAlign>{}));
    STATIC_REQUIRE(supportsArray(std::type_identity<DynamicArray<Atom_t>>{}));
    STATIC_REQUIRE(supportsArray(std::type_identity<StrArray>{}));
    STATIC_REQUIRE(std::is_move_constructible_v<StrArray>);
    STATIC_REQUIRE(std::is_move_assignable_v<StrArray>);
    STATIC_REQUIRE(std::is_copy_assignable_v<StrArray>);
    STATIC_REQUIRE(std::is_copy_constructible_v<StrArray>);
    STATIC_REQUIRE(TriviallyRelocatable<StrArray>);
    using IntVec = DynamicArray<int>;
    using UpVec  = DynamicArray<std::unique_ptr<int>>;
    struct WithDtor {
        using trivially_relocatable            = std::true_type;
        using stringify_value [[maybe_unused]] = std::true_type;
        explicit WithDtor(std::string_view s, std::vector<ConstString>* dt) : str(s), dtors(dt) {}
        ~WithDtor() {
            if (dtors) {
                dtors->emplace_back(std::move(str));
            }
        }
        struct Cmp {
            bool operator()(const WithDtor& lhs, std::string_view rhs) const { return lhs.value() == rhs; }
            bool operator()(const WithDtor& lhs, const ConstString& rhs) const { return lhs.value() == rhs.view(); }
            bool operator()(const ConstString& lhs, std::string_view rhs) const { return lhs.view() == rhs; }
        };
        bool                      operator==(const WithDtor& rhs) const { return str == rhs.str; }
        [[nodiscard]] auto        value() const noexcept -> std::string_view { return str.view(); }
        ConstString               str;
        std::vector<ConstString>* dtors{nullptr};
    };
    struct Int {
        using stringify_value [[maybe_unused]] = std::true_type;
        [[nodiscard]] auto value() const noexcept -> int { return n; }
        int                n = {23};
    };
    static constexpr auto val_cmp = WithDtor::Cmp{};
    STATIC_REQUIRE_FALSE(std::is_trivial_v<Int>);

    using SimpleVec = DynamicArray<Int>;

    SECTION("DefaultConstructor") {
        STATIC_REQUIRE(IntVec{}.empty());
        STATIC_REQUIRE(IntVec{}.size() == 0u); // NOLINT
        STATIC_REQUIRE(IntVec{}.capacity() == 0u);
        STATIC_REQUIRE(sizeof(IntVec) == sizeof(void*) + 8);
        STATIC_REQUIRE(IntVec{}.data() == nullptr);
    }
    SECTION("InitializerListCtor") {
        IntVec v{1, 2, 3, 4};
        REQUIRE(v.size() == 4u);
        REQUIRE(v.capacity() >= v.size());
        for (auto x : std::ranges::views::iota(0u, v.size())) {
            CAPTURE(x);
            REQUIRE(std::cmp_equal(v.at(x), x + 1));
        }

        SimpleVec x{Int{1}, Int{2}};
        REQUIRE(x == std::array{1, 2});
    }
    SECTION("RangeCtor") {
        auto usePointer = GENERATE(true, false);
        CAPTURE(usePointer);
        int       arr[4] = {1, 2, 3, 4};
        std::span span{arr, arr + 4};
        auto      v = usePointer ? IntVec{arr, arr + 4} : IntVec{span.begin(), span.end()};
        REQUIRE(v.size() == static_cast<uint32_t>(span.size()));
        for (auto [idx, x] : enumerate<uint32_t>(span)) {
            CAPTURE(idx);
            REQUIRE(std::cmp_equal(v.at(idx), x));
        }

        REQUIRE(std::ranges::equal(v, arr));
        REQUIRE(std::ranges::equal(std::as_const(v), arr));
        REQUIRE(std::ranges::equal(v.rbegin(), v.rend(), span.rbegin(), span.rend()));
        REQUIRE(std::ranges::equal(v.crbegin(), v.crend(), span.rbegin(), span.rend()));

        std::unique_ptr<int> upArr[3] = {std::make_unique<int>(1), std::make_unique<int>(2), std::make_unique<int>(3)};
        UpVec                vec{std::make_move_iterator(upArr), std::make_move_iterator(upArr + 3)};
        REQUIRE(vec.size() == 3);
        REQUIRE(vec.at(1) != nullptr);
        REQUIRE(*vec.at(1) == 2);
        REQUIRE_FALSE(upArr[1]);

        std::vector<int> stdV({1, 2, 3});
        auto             deduce = DynamicArray(stdV.begin(), stdV.end());
        REQUIRE(deduce == IntVec({1, 2, 3}));

        POTASSCO_WARNING_PUSH()
        POTASSCO_WARNING_IGNORE_MSVC(4244)
        std::vector<double> related({1.3, 2.4, 3.5});
        IntVec              iv(related.begin(), related.end());
        REQUIRE(iv.size() == 3u);
        REQUIRE(iv == std::vector{1, 2, 3});

        std::vector<int> related2({1, 2, 3});
        iv.assign(related2.cbegin(), related2.cend());
        REQUIRE(iv.size() == 3u);
        REQUIRE(iv == std::vector{1, 2, 3});

        std::vector<unsigned> related3({10u, 6u, 1945u});
        iv.assign(related3.begin(), related3.end());
        REQUIRE(iv.size() == 3u);
        REQUIRE(iv == std::vector{10, 6, 1945});
        POTASSCO_WARNING_POP()
    }
    SECTION("AssignCtor") {
        IntVec v(10, 4711);
        REQUIRE(v.size() == 10u);
        auto n = 0u;
        for (auto x : v) {
            REQUIRE(x == 4711);
            ++n;
        }
        REQUIRE(n == 10);

        UpVec uv(3);
        REQUIRE(uv.size() == 3);
        REQUIRE_FALSE(uv.back());

        SimpleVec xv(5);
        REQUIRE(xv.size() == 5);
        REQUIRE(xv.back().n == Int{}.n);
        SimpleVec xx(3, Int{33});
        REQUIRE(xx == std::array{33, 33, 33});

        DynamicArray<void*> ptr(10);
        REQUIRE(ptr.size() == 10);
        REQUIRE(ptr.back() == nullptr);
        ptr.append(2, nullptr);
        REQUIRE(ptr.size() == 12);
        REQUIRE(ptr.back() == nullptr);
        ptr.append(1, (void*) 0x12);
        REQUIRE(ptr.size() == 13);
        REQUIRE(ptr.back() == (void*) 0x12);
        std::array<void*, 2> arr = {(void*) 0x2, (void*) 0x4};
        ptr.append(std::begin(arr), std::end(arr));
        REQUIRE(ptr.size() == 15);
        REQUIRE(ptr.back() == arr[1]);
    }
    SECTION("copy and move") {
        using Array = DynamicArray<WithDtor>;
        std::vector<ConstString> dtors, expected;
        Array                    m1;
        auto                     longStr = "A long long string longer than our SSO buffer size"sv;
        m1.emplace_back(longStr, &dtors);
        m1.emplace_back("Short", &dtors);
        m1.emplace_back("short", &dtors);
        m1.emplace_back(longStr, &dtors);
        expected = {ConstString(longStr), ConstString("Short"), ConstString("short"), ConstString(longStr)};
        REQUIRE(std::ranges::equal(m1, expected, val_cmp));
        REQUIRE(dtors.empty());

        auto  sz  = m1.size();
        auto  cp  = m1.capacity();
        auto* beg = m1.data();

        // NOLINTBEGIN(bugprone-use-after-move)
        SECTION("move construct") {
            Array m2(std::move(m1));
            CHECK(m1.capacity() == 0);
            CHECK(m1.data() == nullptr);
            CHECK(m2.size() == sz);
            CHECK(m2.capacity() == cp);
            CHECK(beg == m2.data());
            REQUIRE(std::ranges::equal(m2, expected, val_cmp));
            REQUIRE(dtors.empty());
            m2.clear();
            REQUIRE(dtors == expected);
        }
        SECTION("copy construct") {
            Array m2(m1);
            CHECK(m2.size() == sz);
            CHECK(m1.size() == sz);
            CHECK(m1.capacity() == cp);
            CHECK(m2.capacity() <= cp);
            CHECK(m1.data() == beg);
            CHECK(m2.data() != beg);
            CHECK(longStr == m2[0].value());
            CHECK(m1[0] == m2[0]);
            CHECK(m1[0].value().data() != m2[0].value().data());

            CHECK(m2 == m1);
            m1.clear();
            REQUIRE(dtors == expected);
            dtors.clear();
            m2.clear();
            REQUIRE(dtors == expected);
        }
        SECTION("assign") {
            Array                    m2;
            std::vector<ConstString> m2ExpectedDtors;
            m2.emplace_back("Foo", &dtors);
            m2.emplace_back("Bar", &dtors);
            m2ExpectedDtors.assign({ConstString("Foo"), ConstString("Bar")});
            SECTION("move") {
                m2 = std::move(m1);
                REQUIRE(dtors == m2ExpectedDtors);
                CHECK(m1.empty());
                CHECK(m1.data() == nullptr);
                CHECK(m2.data() == beg);
                CHECK(m2.size() == sz);
                CHECK(m2.capacity() == cp);
                CHECK(longStr == m2[0].value());
                REQUIRE(std::ranges::equal(m2, expected, val_cmp));
                dtors.clear();
                m2ExpectedDtors = expected;
            }
            SECTION("copy assign") {
                m2 = m1;
                REQUIRE(dtors == m2ExpectedDtors);
                CHECK(m1.size() == sz);
                CHECK(m1.data() == beg);
                CHECK(m2.data() != beg);
                CHECK(m2.size() == sz);
                CHECK(m2.capacity() <= cp);
                CHECK(m2 == m1);
                CHECK(m1[0].value().data() != m2[0].value().data());
                dtors.clear();
                m1.clear();
                REQUIRE(dtors == expected);
                dtors.clear();
                m2ExpectedDtors = expected;
            }
            m2.clear();
            REQUIRE(dtors == m2ExpectedDtors);
        }
        // NOLINTEND(bugprone-use-after-move)
    }

    SECTION("Compare") {
        IntVec v{1, 2, 3, 4, 5};
        REQUIRE(v == v);
        REQUIRE_FALSE(v != v);
        REQUIRE_FALSE(v < v);
        REQUIRE_FALSE(v > v);
        REQUIRE(v <= v);
        REQUIRE(v >= v);
        IntVec cv(v);
        cv.pop_back();
        REQUIRE(v != cv);
        REQUIRE(cv < v);
        REQUIRE(v > cv);
        cv.push_back(v.back());
        ++cv[2];
        REQUIRE(v != cv);
        REQUIRE(cv > v);
        REQUIRE(v < cv);
    }
    SECTION("ReserveResize") {
        UpVec up;
        up.resize(3);
        REQUIRE(up.size() == 3u);
        REQUIRE(up.capacity() >= 3u);
        auto c = up.capacity();
        up.resize(c);
        REQUIRE(up.size() == c);
        REQUIRE(up.capacity() == c);

        REQUIRE_NOTHROW(up.reserve(2u));
        REQUIRE(up.capacity() >= 3u);

        IntVec vec;
        vec.resize(5, 47);
        auto cap = vec.capacity();
        REQUIRE(vec.size() == 5u);
        REQUIRE(vec == std::vector<int>(5, 47));
        vec.resize(3u);
        REQUIRE(vec == std::vector<int>(3, 47));
        REQUIRE(vec.capacity() == cap);

        vec.resize(cap, 93);
        REQUIRE(vec.size() == cap);
        REQUIRE(vec.capacity() == cap);

        vec.reserve(3u);
        REQUIRE(vec.capacity() == cap);

        vec.reserve(cap + 3);
        auto exp = cap + 3;
        REQUIRE(vec.capacity() >= exp);
    }

    SECTION("Push") {
        DynamicArray<std::pair<int, int>> r;
        r.push_back(std::pair(1, 2));
        auto lv = std::pair{3, 4};
        r.push_back(lv);
        r.emplace_back(5, 6);
        CHECK(r.size() == 3u);
        CHECK(r.at(0) == std::pair(1, 2));
        CHECK(r.at(1) == std::pair(3, 4));
        CHECK(r.at(2) == std::pair(5, 6));

        IntVec vec;
        vec.push_back(1);
        REQUIRE(vec.size() == 1u);
        REQUIRE(vec.capacity() > 1u);
        auto c = vec.capacity();
        while (vec.size() < vec.capacity()) {
            vec.push_back(static_cast<int>(vec.size()));
            REQUIRE(vec.capacity() == c);
        }
        vec.push_back(0);
        REQUIRE(vec.capacity() > c);
        REQUIRE(vec.back() == 0);

        SimpleVec xx;
        xx.push_back(Int{12});
        xx.push_back(Int{13});
        xx.push_back(Int{14});
        REQUIRE(xx == std::array{12, 13, 14});

        UpVec up;
        up.reserve(1u);
        up.emplace_back(std::make_unique<int>(99));
        up.emplace_back(std::make_unique<int>(77));
        REQUIRE(up.size() == 2u);
        REQUIRE(up.front());
        REQUIRE(up.back());
        REQUIRE(*up.back() == 77);

        up.resize(1u);
        REQUIRE(up.size() == 1u);
        up.resize(3u);
        REQUIRE(up.size() == 3u);
        REQUIRE_FALSE(up.back());

        up.back() = std::make_unique<int>(47);
        up.reserve(77);
        REQUIRE(up.capacity() >= 77);
        REQUIRE(up.size() == 3u);
        REQUIRE(up.back());
        REQUIRE(*up.back() == 47);
    }
    SECTION("InsertAndEmplace") {
        UpVec up;
        auto  expected = std::vector<int>{};
        auto  type     = GENERATE("in-place"sv, "grow"sv);
        CAPTURE(type);
        for (int i = 0; expected.size() < 4 || up.size() < up.capacity(); ++i) {
            expected.push_back(i);
            up.push_back(std::make_unique<int>(i));
        }
        REQUIRE(up.size() == up.capacity());
        if (type == "in-place"sv) {
            up.reserve(up.size() + 1);
        }
        auto insIdx   = 2;
        auto pos      = up.begin() + insIdx;
        auto action   = GENERATE("insert"sv, "emplace"sv);
        auto doInsert = [&](std::unique_ptr<int>&& val) {
            return action == "insert"sv ? up.insert(pos, std::move(val)) : up.emplace(pos, std::move(val));
        };
        CAPTURE(action);

        IntVec iv(expected.begin(), expected.end());
        expected.insert(expected.begin() + insIdx, 99);
        pos = doInsert(std::make_unique<int>(99));
        REQUIRE(std::cmp_equal(up.size(), expected.size()));
        REQUIRE(pos->get());
        REQUIRE(**pos == 99);
        REQUIRE(up == expected);

        using DtorVec = DynamicArray<WithDtor>;
        std::vector<ConstString> dtors;
        DtorVec                  dt;
        dt.emplace_back("A", &dtors);
        dt.emplace_back("C", &dtors);
        dt.emplace_back("D", &dtors);
        if (action == "insert"sv) {
            IntVec iv2(iv);
            auto   p = iv.insert(iv.begin() + insIdx, 99);
            REQUIRE(*p == 99);
            REQUIRE(*iv2.insert(iv2.begin() + insIdx, *p) == 99);
            REQUIRE(iv == expected);
            REQUIRE(iv2 == expected);

            auto s = dt.insert(dt.begin() + 1, WithDtor{"B", nullptr});
            REQUIRE(s->value() == "B");
            REQUIRE(s->dtors == nullptr);
            s->dtors = &dtors;
            REQUIRE(std::ranges::equal(dt, std::array{"A"sv, "B"sv, "C"sv, "D"sv}, val_cmp));
            REQUIRE(dtors.empty());
            dt.clear();
            REQUIRE(std::ranges::equal(dtors, std::array{"A"sv, "B"sv, "C"sv, "D"sv}, val_cmp));
        }
        else {
            REQUIRE(*iv.emplace(iv.begin() + insIdx, 99) == 99);
            REQUIRE(iv == expected);

            auto s = dt.emplace(dt.begin() + 1, "B", &dtors);
            REQUIRE(s->value() == "B");
            REQUIRE(s->dtors == &dtors);
            REQUIRE(std::ranges::equal(dt, std::array{"A"sv, "B"sv, "C"sv, "D"sv}, val_cmp));
            REQUIRE(dtors.empty());
            dt.clear();
            REQUIRE(std::ranges::equal(dtors, std::array{"A"sv, "B"sv, "C"sv, "D"sv}, val_cmp));
        }
    }
    SECTION("append") {
        DynamicArray<int> r;
        std::vector<int>  e(10, 998);
        r.append(10, 998);
        CHECK(r.size() == 10);
        CHECK(r == e);
        r.append({1, 2, 3, 4});
        e.insert(e.end(), {1, 2, 3, 4});
        CHECK(r.size() == 14);
        CHECK(r == e);

        auto sp = r.appendForOverwrite(2);
        CHECK(r.size() == 16);
        REQUIRE(sp.size() == 2);
        sp[0] = 47;
        sp[1] = 11;
        e.insert(e.end(), {47, 11});
        CHECK(r == e);
    }
    SECTION("ShrinkToFit") {
        IntVec vec(1024u, 99);
        REQUIRE(vec.size() == 1024u);
        REQUIRE(vec.capacity() >= 1024u);
        vec.resize(4u);
        vec.shrink_to_fit();
        REQUIRE(vec.capacity() < 1024u);
        REQUIRE(vec == std::array{99, 99, 99, 99});
        vec.clear();
        vec.shrink_to_fit();
        REQUIRE(vec.capacity() == 0u);
        REQUIRE(vec.data() == nullptr);

        UpVec up(1024u);
        up[2]  = std::make_unique<int>(2);
        up[4]  = std::make_unique<int>(4);
        up[99] = std::make_unique<int>(292);
        up.resize(5);
        REQUIRE(up.capacity() >= 1024u);
        up.shrink_to_fit();
        REQUIRE(up.capacity() < 1024u);
        REQUIRE(up[2]);
        REQUIRE(up[4]);
        REQUIRE(*up[2] == 2);
        REQUIRE(*up[4] == 4);

        vec.resize(20);
        for (int i = 0; i != 100; ++i) {
            vec.push_back(i);
            if (vec.capacity() > vec.size()) {
                break;
            }
        }
        REQUIRE(vec.size() < 100u);
        REQUIRE(vec.capacity() > vec.size());
        auto oldC = vec.capacity();
        auto oldS = vec.size();
        vec.resize(vec.capacity(), 999);
        vec.resize(oldS);
        auto* oldData = vec.data();
        vec.shrink_to_fit();
        REQUIRE((oldData == vec.data() || vec.capacity() < oldC));

        vec.resize(99, 4711);
        REQUIRE(vec.size() == 99);
        REQUIRE(vec.data() != nullptr);
        vec.reset();
        REQUIRE(vec.capacity() == 0u);
        REQUIRE(vec.size() == 0u); // NOLINT
        REQUIRE(vec.data() == nullptr);

        std::vector<ConstString> dtors, expected;
        DynamicArray<WithDtor>   arr;
        arr.emplace_back("a", &dtors);
        auto c = arr.capacity();
        while (arr.capacity() == c) {
            arr.emplace_back(std::string(arr.size() + 1, 'x'), &dtors);
            expected.emplace_back(arr.back().value());
        }
        arr.pop(arr.size() - 1);
        REQUIRE(arr.size() == 1u);
        REQUIRE(arr.back().value() == "a");
        REQUIRE(dtors == expected);
        arr.shrink_to_fit();
        REQUIRE(arr.capacity() <= c);
        REQUIRE(dtors == expected);
        arr.clear();
        expected.emplace_back("a");
        REQUIRE(dtors == expected);
    }

    SECTION("Swap") {
        IntVec vec{1, 2, 3, 4, 5};
        IntVec{}.swap(vec);
        REQUIRE(vec.empty());
        REQUIRE(vec.capacity() == 0u);

        vec.assign({1, 2, 3, 4});
        IntVec other{5, 6, 7, 8};
        vec.swap(other);
        REQUIRE(vec == std::array{5, 6, 7, 8});
        REQUIRE(other == std::array{1, 2, 3, 4});

        swap(vec, other);
        REQUIRE(vec == std::array{1, 2, 3, 4});
        REQUIRE(other == std::array{5, 6, 7, 8});
    }
    SECTION("Append") {
        IntVec vec;
        vec.append(3, 8);
        REQUIRE(vec.size() == 3u);
        REQUIRE(vec == std::vector{8, 8, 8});
        vec.append(2, 9);
        REQUIRE(vec.size() == 5u);
        REQUIRE(vec == std::vector{8, 8, 8, 9, 9});
        auto x = {47, 11};
        vec.append(x.begin(), x.end());
        REQUIRE(vec.size() == 7u);
        REQUIRE(vec == std::vector{8, 8, 8, 9, 9, 47, 11});
    }
    SECTION("Erase") {
        std::vector<ConstString> dtors, expected;
        IntVec                   vec{1, 2, 3, 4, 5, 6};
        REQUIRE(vec.size() == 6);
        using DtorVec = DynamicArray<WithDtor>;
        DtorVec dt;
        dt.emplace_back("A", &dtors);
        dt.emplace_back("B", &dtors);
        dt.emplace_back("C", &dtors);
        dt.emplace_back("D", &dtors);
        dt.emplace_back("E", &dtors);
        REQUIRE(dtors.empty());
        SECTION("single") {
            SECTION("front") {
                auto p = vec.erase(vec.begin());
                REQUIRE(*p == 2);
                REQUIRE(vec.size() == 5u);
                REQUIRE(vec == std::array{2, 3, 4, 5, 6});

                expected.emplace_back(dt.front().str);
                auto dp = dt.erase(dt.begin());
                REQUIRE(dp->str == "B");
                REQUIRE(dt.size() == 4u);
                REQUIRE(dtors == expected);
                REQUIRE(std::ranges::equal(dt, std::array{"B"sv, "C"sv, "D"sv, "E"sv}, val_cmp));
            }
            SECTION("back") {
                auto p = vec.erase(vec.end() - 1);
                REQUIRE(p == vec.end());
                REQUIRE(vec == std::array{1, 2, 3, 4, 5});
                REQUIRE(vec.size() == 5u);

                expected.emplace_back(dt.back().str);
                auto dp = dt.erase(dt.end() - 1);
                REQUIRE(dp == dt.end());
                REQUIRE(dt.size() == 4u);
                REQUIRE(dtors == expected);
                REQUIRE(std::ranges::equal(dt, std::array{"A"sv, "B"sv, "C"sv, "D"sv}, val_cmp));
            }
            SECTION("middle") {
                auto p = vec.erase(vec.begin() + 3);
                REQUIRE(*p == 5);
                REQUIRE(vec == std::array{1, 2, 3, 5, 6});
                REQUIRE(vec.size() == 5u);
                p = vec.erase(vec.begin() + 2);
                REQUIRE(*p == 5);
                REQUIRE(vec.size() == 4u);
                REQUIRE(vec == std::array{1, 2, 5, 6});

                expected.emplace_back(dt[3].value());
                auto dp = dt.erase(dt.begin() + 3);
                REQUIRE(dp->str.view() == "E");
                REQUIRE(dt.size() == 4u);
                expected.emplace_back(dt[1].value());
                dp = dt.erase(dt.begin() + 1);
                REQUIRE(dp->str.view() == "C");
                REQUIRE(dt.size() == 3u);
                REQUIRE(dtors == expected);
                REQUIRE(std::ranges::equal(dt, std::array{"A"sv, "C"sv, "E"sv}, val_cmp));
            }
        }
        SECTION("algo") {
            vec.push_back(1);
            vec[3]    = 1;
            dt[2].str = ConstString("A");
            dt[3].str = ConstString("A");
            dt.emplace_back("A", &dtors);
            auto value = GENERATE(true, false);
            CAPTURE(value);
            if (value) {
                REQUIRE(erase(vec, 1) == 3);
                REQUIRE(erase(dt, WithDtor("A", nullptr)) == 4);
            }
            else {
                REQUIRE(erase_if(vec, [](int v) { return v == 1; }) == 3);
                REQUIRE(erase_if(dt, [](const WithDtor& x) { return x.value() == "A"; }) == 4);
            }
            REQUIRE(vec == std::array{2, 3, 5, 6});
            expected.assign(4, ConstString("A"));
            REQUIRE(dtors == expected);
            REQUIRE(std::ranges::equal(dt, std::array{"B"sv, "E"sv}, val_cmp));
        }
    }
}

TEST_CASE("Test HashMap", "[util]") {
    using MapT = SimpleHashMap<int, double, INT32_MAX>;
    STATIC_CHECK(TriviallyRelocatable<MapT>);
    STATIC_CHECK_FALSE(TriviallyRelocatable<DynamicHashTable<int, std::string, Detail::NumHashTraits<int, 0>>>);

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
    static_assert(TriviallyRelocatable<DynamicIndex>, "should be trivially relocatable");

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
    static_assert(TriviallyRelocatable<OrderedStringSet>, "should be trivially relocatable");

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
        using D = std::pair<std::string_view, unsigned>;
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
}

} // namespace Potassco::Test::Utils
