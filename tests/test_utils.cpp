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
template <typename T, typename Alloc, std::unsigned_integral SizeT, std::ranges::sized_range R>
static auto operator==(const Vector<T, Alloc, SizeT>& lhs, const R& rhs) -> decltype(std::ranges::equal(lhs, rhs)) {
    return std::ranges::equal(lhs, rhs);
}
template <typename T, typename Alloc, std::unsigned_integral SizeT, typename U = decltype(std::declval<T>().value()),
          std::ranges::sized_range R>
requires std::same_as<std::ranges::range_value_t<R>, U>
static auto operator==(const Vector<T, Alloc, SizeT>& lhs, const R& rhs) -> bool {
    return std::ranges::equal(lhs, rhs, [](const T& x, const U& y) { return x.value() == y; });
}
template <typename Alloc, std::unsigned_integral SizeT, std::ranges::sized_range R>
requires std::same_as<std::ranges::range_value_t<R>, int>
static auto operator==(const Vector<std::unique_ptr<int>, Alloc, SizeT>& lhs, const R& rhs) -> bool {
    return std::ranges::equal(lhs, rhs, [](const auto& x, int y) { return x ? *x == y : y == -1; });
}

} // namespace Potassco
namespace Potassco::Test::Utils {
namespace {
template <typename T>
concept VectorCompatible = requires { typename Vector<T>; };
} // namespace
TEST_CASE("Test Traits", "[util]") {
    STATIC_REQUIRE(is_trivially_relocatable_v<Atom_t>);
    STATIC_REQUIRE(is_trivially_relocatable_v<Lit_t>);
    STATIC_REQUIRE(is_trivially_relocatable_v<Weight_t>);
    STATIC_REQUIRE(is_trivially_relocatable_v<WeightLit>);
    STATIC_REQUIRE(is_trivially_relocatable_v<HeadType>);
    STATIC_REQUIRE(is_trivially_relocatable_v<AtomSpan>);
    STATIC_REQUIRE(is_trivially_relocatable_v<ConstString>);
    STATIC_REQUIRE(is_trivially_relocatable_v<std::unique_ptr<Atom_t>>);
    STATIC_REQUIRE(is_trivially_relocatable_v<std::unique_ptr<std::string>>);
    STATIC_REQUIRE(is_trivially_relocatable_v<std::pair<Atom_t, Lit_t>>);
    STATIC_REQUIRE(is_trivially_relocatable_v<Vector<Atom_t>>);

    STATIC_REQUIRE_FALSE(is_trivially_relocatable_v<std::string>);
    STATIC_REQUIRE_FALSE(is_trivially_relocatable_v<std::pair<int, std::string>>);
}
TEST_CASE("Test Vector", "[util]") {
    struct alignas(alignof(max_align_t) * 2) HugeAlign {
        int x;
    };
    STATIC_REQUIRE(VectorCompatible<Atom_t>);
    STATIC_REQUIRE_FALSE(VectorCompatible<std::string>);
    STATIC_REQUIRE(VectorCompatible<HugeAlign>);
    STATIC_REQUIRE(VectorCompatible<Vector<Atom_t>>);
    using IntVec = Vector<int>;
    using UpVec  = Vector<std::unique_ptr<int>>;
    struct WithDtor {
        using trivially_relocatable            = std::true_type;
        using stringify_value [[maybe_unused]] = std::true_type;
        explicit WithDtor(int x, std::vector<int>* dt) : n(x), dtors(dt) {}
        ~WithDtor() {
            if (dtors) {
                dtors->push_back(n);
            }
        }
        [[nodiscard]] auto value() const noexcept -> int { return n; }
        int                n;
        std::vector<int>*  dtors{nullptr};
    };
    struct Int {
        using stringify_value [[maybe_unused]] = std::true_type;
        [[nodiscard]] auto value() const noexcept -> int { return n; }
        int                n = {23};
    };
    STATIC_REQUIRE_FALSE(std::is_trivial_v<Int>);
    using DtorVec   = Vector<WithDtor>;
    using SimpleVec = Vector<Int>;

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
        auto             deduce = Vector(stdV.begin(), stdV.end());
        REQUIRE(deduce == IntVec({1, 2, 3}));
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
    }
    SECTION("Copy") {
        IntVec v1{1, 2, 3, 4, 5};
        SECTION("Ctor") {
            IntVec v2(v1);
            REQUIRE(v1.size() == v2.size());
            auto n = 0u;
            for (auto [idx, x] : enumerate(v1)) {
                REQUIRE(v2.at(idx) == x);
                ++n;
            }
            REQUIRE(n == v1.size());
            REQUIRE(v1 == v2);
        }
        SECTION("Assign") {
            IntVec v2{6, 7, 8};

            v2 = v1;
            REQUIRE(v1.size() == v2.size());
            auto n = 0u;
            for (auto [idx, x] : enumerate(v1)) {
                REQUIRE(v2.at(idx) == x);
                ++n;
            }
            REQUIRE(n == v1.size());
            REQUIRE(v1 == v2);
        }
    }
    SECTION("Move") {
        IntVec v1{1, 2, 3, 4, 5};
        // NOLINTBEGIN(bugprone-use-after-move)
        SECTION("CtorRegular") {
            IntVec v2(std::move(v1));
            REQUIRE(v1.empty());
            REQUIRE(v1.capacity() == 0u);
            REQUIRE(v2.size() == 5);
            REQUIRE(v2 == IntVec{1, 2, 3, 4, 5});
        }
        SECTION("AssignRegular") {
            IntVec v2({6, 8, 8});
            v2 = std::move(v1);
            REQUIRE(v1.empty());
            REQUIRE(v1.capacity() == 0u);
            REQUIRE(v2.size() == 5);
            REQUIRE(v2 == std::array{1, 2, 3, 4, 5});
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
    SECTION("PushBack") {
        IntVec vec;
        REQUIRE(vec.empty());
        REQUIRE(vec.capacity() == 0u);
        vec.push_back(1);
        REQUIRE(vec.size() == 1u);
        REQUIRE(vec.capacity() >= 1u);
        while (vec.size() < vec.capacity()) { vec.push_back(static_cast<int>(vec.size())); }
        auto x = vec.back();
        vec.push_back(vec.back());
        REQUIRE(vec.capacity() > vec.size());
        REQUIRE(vec.back() == x);

        vec.pop_back();
        vec.pop_back();
        REQUIRE(vec.back() < x);

        SimpleVec xx;
        xx.push_back(Int{12});
        xx.push_back(Int{13});
        xx.push_back(Int{14});
        REQUIRE(xx == std::array{12, 13, 14});
    }
    SECTION("EmplaceBack") {
        Vector<std::pair<int, double>> vec;
        vec.reserve(1u);
        REQUIRE(vec.emplace_back(1, 2.0) == std::pair{1, 2.0});
        REQUIRE(vec.size() == 1u);
        REQUIRE(vec.capacity() == 1u);

        vec.back().first = 99;
        vec.emplace_back(vec.back());
        REQUIRE(vec.size() == 2u);
        REQUIRE(vec.back() == std::pair{99, 2.0});
        REQUIRE(vec.back() == vec.front());

        vec.resize(1u, std::pair{12, 3.0});
        REQUIRE(vec.size() == 1u);

        UpVec up;
        up.reserve(1u);
        up.emplace_back(std::make_unique<int>(99));
        up.emplace_back(std::move(up.back()));
        REQUIRE(up.size() == 2u);
        REQUIRE_FALSE(up.front());
        REQUIRE(up.back());
        REQUIRE(*up.back() == 99);

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
    SECTION("PushPopClear") {
        IntVec vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
        vec.pop_back();
        REQUIRE(vec.size() == 2);
        REQUIRE(vec == std::array{1, 2});
        vec.clear();
        REQUIRE(vec.empty());
        REQUIRE(vec.capacity() >= 3u);

        UpVec up(5);
        up[2] = std::make_unique<int>(99);
        up[4] = std::make_unique<int>(77);
        up.pop_back();
        up.pop_back();
        REQUIRE(up.size() == 3u);
        REQUIRE(up[2]);
        REQUIRE(*up[2] == 99);
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
        auto insIdx = 2;
        auto pos    = up.begin() + insIdx;
        SECTION("one") {
            auto action   = GENERATE("insert"sv, "emplace"sv);
            auto doInsert = [&](std::unique_ptr<int>&& val) {
                return action == "insert"sv ? up.insert(pos, std::move(val)) : up.emplace(pos, std::move(val));
            };
            CAPTURE(action);
            SECTION("new") {
                IntVec iv(expected.begin(), expected.end());
                expected.insert(expected.begin() + insIdx, 99);
                pos = doInsert(std::make_unique<int>(99));
                REQUIRE(std::cmp_equal(up.size(), expected.size()));
                REQUIRE(pos->get());
                REQUIRE(**pos == 99);
                REQUIRE(up == expected);

                if (action == "insert"sv) {
                    IntVec iv2(iv);
                    auto   p = iv.insert(iv.begin() + insIdx, 99);
                    REQUIRE(*p == 99);
                    REQUIRE(*iv2.insert(iv2.begin() + insIdx, *p) == 99);
                    REQUIRE(iv == expected);
                    REQUIRE(iv2 == expected);
                }
                else {
                    REQUIRE(*iv.emplace(iv.begin() + insIdx, 99) == 99);
                    REQUIRE(iv == expected);
                }
            }
            SECTION("from-self") {
                expected.insert(expected.begin() + insIdx, 3);
                expected[static_cast<unsigned>(insIdx + 2)] = -1;
                pos                                         = doInsert(std::move(*(pos + 1)));
                REQUIRE(std::cmp_equal(up.size(), expected.size()));
                REQUIRE(pos->get());
                REQUIRE(**pos == 3);
                REQUIRE(up == expected);

                Vector<std::pair<int, int>> pv;
                for (auto x : expected) { pv.emplace_back(x, x); }
                while (pv.size() < pv.capacity()) {
                    pv.emplace_back(static_cast<int>(pv.size()), static_cast<int>(pv.size()));
                }
                if (type == "in-place"sv) {
                    pv.reserve(pv.size() + 1);
                }
                auto ep = std::pair{pv.front().first, pv.back().second};
                auto xp = pv.emplace(pv.begin() + 2, pv.front().first, pv.back().second);
                REQUIRE(*xp == ep);
            }
        }
        SECTION("many") {
            IntVec iv{expected.begin(), expected.end()};
            if (type == "in-place"sv) {
                iv.reserve(static_cast<uint32_t>(expected.size() + 5));
            }
            SECTION("new") {
                expected.insert(expected.begin() + insIdx, 5, 99);
                auto ivPos = iv.insert(iv.begin() + insIdx, 5, 99);
                REQUIRE(std::cmp_equal(iv.size(), expected.size()));
                REQUIRE(*ivPos == 99);
                REQUIRE(iv == expected);
            }
            SECTION("from-self") {
                expected.insert(expected.begin() + insIdx, 5, 1);
                auto ivPos = iv.insert(iv.begin() + insIdx, 5, iv[1]);
                REQUIRE(std::cmp_equal(iv.size(), expected.size()));
                REQUIRE(*ivPos == 1);
                REQUIRE(iv == expected);
            }
        }
    }
    SECTION("InsertRange") {
        IntVec vec{1, 2, 3, 4, 5};
        int    arr[3] = {97, 98, 99};

        std::vector<int> iExpected{1, 2, 3, 4, 5};
        while (vec.capacity() > vec.size()) {
            vec.push_back(static_cast<int>(vec.size()) + 1);
            iExpected.push_back(vec.back());
        }
        REQUIRE(vec.capacity() == vec.size());
        iExpected.insert(iExpected.begin() + 2, std::begin(arr), std::end(arr));

        UpVec up;
        up.reserve(vec.size());
        std::vector<int> uExpected;
        for (auto i : vec) {
            up.emplace_back(std::make_unique<int>(i));
            uExpected.push_back(i);
        }
        while (up.capacity() > up.size()) {
            up.emplace_back(std::make_unique<int>(static_cast<int>(up.size()) + 1));
            uExpected.push_back(*up.back());
        }
        std::unique_ptr<int> upArr[2] = {std::make_unique<int>(4711), std::make_unique<int>(4819)};
        REQUIRE(up.capacity() == up.size());
        auto x = uExpected.insert(uExpected.begin() + 4, 2u, 0);
        *x++   = 4711;
        *x++   = 4819;

        bool inplace = GENERATE(true, false);
        CAPTURE(inplace);
        if (inplace) {
            vec.reserve(vec.size() + 3u);
        }
        auto p = vec.insert(vec.begin() + 2, std::begin(arr), std::end(arr));
        REQUIRE(*p == 97);
        REQUIRE(vec == iExpected);

        up.reserve(up.size() + 2u);
        REQUIRE(**up.insert(up.begin() + 4u, std::make_move_iterator(upArr), std::make_move_iterator(upArr + 2)) ==
                4711);
        REQUIRE(up == uExpected);
    }

    SECTION("Erase") {
        std::vector<int> dtors;
        IntVec           vec{1, 2, 3, 4, 5, 6};
        REQUIRE(vec.size() == 6);
        DtorVec dt;
        for (int i = 0; i != 6; ++i) { dt.emplace_back(i + 1, &dtors); }
        REQUIRE(dtors.empty());
        SECTION("single") {
            SECTION("front") {
                auto p = vec.erase(vec.begin());
                REQUIRE(*p == 2);
                REQUIRE(vec == std::array{2, 3, 4, 5, 6});
                REQUIRE(vec.size() == 5u);
                auto dp = dt.erase(dt.begin());
                REQUIRE(dp->n == 2);
                REQUIRE(dtors == std::vector({1}));
            }
            SECTION("back") {
                auto p = vec.erase(vec.end() - 1);
                REQUIRE(p == vec.end());
                REQUIRE(vec == std::array{1, 2, 3, 4, 5});
                REQUIRE(vec.size() == 5u);

                auto dp = dt.erase(dt.end() - 1);
                REQUIRE(dp == dt.end());
                REQUIRE(dtors == std::vector({6}));
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

                auto dp = dt.erase(dt.begin() + 3);
                REQUIRE(dp->n == 5);
                REQUIRE(dt.size() == 5u);
                dp = dt.erase(dt.begin() + 2);
                REQUIRE(dp->n == 5);
                REQUIRE(dt.size() == 4u);
                REQUIRE(dtors == std::vector({4, 3}));
                REQUIRE(dt == vec);
            }
        }
        SECTION("range") {
            SECTION("front") {
                auto p = vec.erase(vec.begin(), vec.begin() + 3);
                REQUIRE(*p == 4);
                REQUIRE(vec == std::array{4, 5, 6});
                REQUIRE(vec.size() == 3u);

                auto dp = dt.erase(dt.begin(), dt.begin() + 3);
                REQUIRE(dp->n == 4);
                REQUIRE(dtors == std::vector({1, 2, 3}));
                REQUIRE(dt == vec);
            }
            SECTION("back") {
                auto p = vec.erase(vec.end() - 2, vec.end());
                REQUIRE(p == vec.end());
                REQUIRE(vec == std::array{1, 2, 3, 4});
                REQUIRE(vec.size() == 4u);

                auto dp = dt.erase(dt.end() - 2, dt.end());
                REQUIRE(dp == dt.end());
                REQUIRE(dtors == std::vector({5, 6}));
                REQUIRE(dt == vec);
            }
            SECTION("middle") {
                auto p = vec.erase(vec.begin() + 1, vec.begin() + 4);
                REQUIRE(*p == 5);
                REQUIRE(vec == std::array{1, 5, 6});
                REQUIRE(vec.size() == 3u);

                auto dp = dt.erase(dt.begin() + 1, dt.begin() + 4);
                REQUIRE(dp->n == 5);
                REQUIRE(dtors == std::vector({2, 3, 4}));
                REQUIRE(vec.size() == 3u);
                REQUIRE(dt == vec);
            }
        }
        SECTION("algo") {
            vec[3]  = 1;
            vec[5]  = 1;
            dt[3].n = 1;
            dt[5].n = 1;
            SECTION("byValue") {
                REQUIRE(erase(vec, 1) == 3);
                REQUIRE(vec == std::array{2, 3, 5});
            }
            SECTION("byFilter") {
                REQUIRE(erase_if(vec, [](int v) { return v == 1; }) == 3);
                REQUIRE(vec == std::array{2, 3, 5});

                REQUIRE(erase_if(dt, [](const WithDtor& x) { return x.n == 1; }) == 3);
                REQUIRE(dt == vec);
                REQUIRE(dtors == std::vector({1, 1, 1}));
            }
        }
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
    SECTION("StandardAlloc") {
        Vector<int, std::allocator<int>> v{1, 2, 3, 4};
        REQUIRE(v == std::vector{1, 2, 3, 4});
        auto copy(v);
        REQUIRE(copy == v);
        auto move(std::move(v));
        REQUIRE(move == copy);
        REQUIRE(v.empty()); // NOLINT
        v.assign({7, 8, 9});
        v = copy;
        REQUIRE(copy == v);
        v.clear();
        v = std::move(move);
        REQUIRE(move.empty()); // NOLINT
        REQUIRE(v == copy);
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

} // namespace Potassco::Test::Utils
