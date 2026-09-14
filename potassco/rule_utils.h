//
// Copyright (c) 2016 - present, Benjamin Kaufmann
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
#include <potassco/utils.h>

namespace Potassco {
/*!
 * \addtogroup BasicTypes
 */
///@{

//! A sum aggregate with a lower bound.
struct Sum {
    WeightLitSpan lits;  //!< Weight literals of the aggregate.
    Weight_t      bound; //!< Lower bound of the aggregate.
};
//! A type that can represent an aspif rule.
struct Rule {
    constexpr Rule() {}
    HeadType ht{HeadType::disjunctive}; //!< Head type of the rule.
    AtomSpan head;                      //!< Head atoms of the rule.
    BodyType bt{BodyType::normal};      //!< Type of rule body.
    union {
        LitSpan cond{};
        Sum     agg;
    };
    //! Named constructor for creating a rule.
    static Rule normal(HeadType ht, AtomSpan head, LitSpan body);
    //! Named constructor for creating a sum rule.
    static Rule sum(HeadType ht, AtomSpan head, const Sum& sum);
    //! Named constructor for creating a sum rule.
    static Rule sum(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan lits);
    //! Returns whether the rule has a normal body (i.e., conjunction of literals).
    [[nodiscard]] bool normal() const { return bt == BodyType::normal; }
    //! Returns whether the body of the rule is a sum aggregate.
    [[nodiscard]] bool sum() const { return bt != BodyType::normal; }
};

//! A builder class for creating a rule.
class RuleBuilder {
public:
    POTASSCO_TRIVIALLY_RELOCATABLE();

    RuleBuilder()                         = default;
    RuleBuilder(const RuleBuilder& other) = default;
    RuleBuilder(RuleBuilder&& other) noexcept;
    ~RuleBuilder()                                           = default;
    auto operator=(const RuleBuilder& other) -> RuleBuilder& = default;
    auto operator=(RuleBuilder&& other) noexcept -> RuleBuilder&;
    void swap(RuleBuilder& other) noexcept;
    /*!
     * \name Start functions.
     * Functions for starting the definition of a rule's head or body.
     * If the active rule already has a head/body, the active rule is discarded.
     * \note The body of a rule can be defined before or after its head is defined, but definitions
     *       of head and body must not overlap.
     */
    //@{
    //! Start definition of the rule's head, which can be either disjunctive or a choice.
    auto start(HeadType ht = HeadType::disjunctive) -> RuleBuilder&;
    //! Start definition of a `minimize` rule. No head allowed.
    auto startMinimize(Weight_t prio) -> RuleBuilder&;
    //! Start definition of a conjunction to be used as the rule's body.
    auto startBody() -> RuleBuilder&;
    //! Start definition of a sum aggregate to be used as the rule's body.
    auto startSum(Weight_t bound) -> RuleBuilder&;
    //! Update lower bound of sum aggregate.
    auto setBound(Weight_t bound) -> RuleBuilder&;
    //@}

    /*!
     * \name Update functions.
     * Functions for adding elements to the active part of the rule.
     */
    //@{
    //! Add the given atom to the rule's head.
    auto addHead(Atom_t a) -> RuleBuilder&;
    //! Allocate and return space for `n` head atoms.
    auto allocHeadAtoms(uint32_t n) -> std::span<Atom_t> { return allocRange<Atom_t>(head_, body_.end, "Head", n); }
    //! Add lit to the rule's body.
    auto addGoal(Lit_t lit) -> RuleBuilder&;
    auto addGoal(WeightLit lit) -> RuleBuilder&;
    auto addGoal(Lit_t lit, Weight_t w) -> RuleBuilder& { return addGoal(WeightLit{.lit = lit, .weight = w}); }
    //! Allocate and return space for `n` body literals.
    auto allocBodyGoals(uint32_t n) -> std::span<Lit_t> {
        POTASSCO_CHECK_PRE(bodyType() == BodyType::normal);
        return allocRange<Lit_t>(body_, head_.end, "Body", n);
    }
    auto allocSumGoals(uint32_t n) -> std::span<WeightLit> {
        POTASSCO_CHECK_PRE(bodyType() != BodyType::normal);
        return allocRange<WeightLit>(body_, head_.end, "Sum", n);
    }
    //@}

    //! Stop definition of rule and add rule to out if given.
    auto end(AbstractProgram* out = nullptr) -> RuleBuilder&;
    //! Discard the active rule.
    auto clear() -> RuleBuilder&;
    //! Discard the body of the active rule but keep the head if any.
    auto clearBody() -> RuleBuilder&;
    //! Discard the head of the active rule but keep the body if any.
    auto clearHead() -> RuleBuilder&;
    //! Weaken the active sum aggregate to a normal body or count aggregate.
    auto weaken(BodyType to, bool resetWeights = true) -> RuleBuilder&;

    /*!
     * \name Query functions.
     * Functions for accessing parts of the active rule.
     * \note The result of these functions is only valid until the next call to an update function.
     */
    //@{
    [[nodiscard]] auto headType() const -> HeadType { return static_cast<HeadType>(type(head_)); }
    [[nodiscard]] auto head() const -> AtomSpan;
    [[nodiscard]] auto isMinimize() const -> bool;
    [[nodiscard]] auto bodyType() const -> BodyType { return static_cast<BodyType>(type(body_)); }
    [[nodiscard]] auto body() const -> LitSpan;
    [[nodiscard]] auto bound() const -> Weight_t;
    [[nodiscard]] auto sumLits() const -> std::span<WeightLit>;
    [[nodiscard]] auto findSumLit(Lit_t lit) const -> WeightLit*;
    [[nodiscard]] auto sum() const -> Sum;
    [[nodiscard]] auto rule() const -> Rule;
    [[nodiscard]] auto isFact() const -> bool;
    //@}
private:
    struct Range {
        [[nodiscard]] bool open() const noexcept { return start == 0u; }
        [[nodiscard]] auto size() const noexcept -> uint32_t { return end - start; }

        uint32_t start = 0u;
        uint32_t end   = 0u;
    };
    [[nodiscard]] auto mem() const noexcept -> std::byte* { return const_cast<std::byte*>(mem_.data()); }
    [[nodiscard]] auto type(const Range& r) const noexcept -> uint32_t;

    void start(Range& r, uint32_t type, const Weight_t* bound = nullptr);
    void clear(Range& r, uint32_t oPos);
    template <typename T>
    auto append(const T& elem) -> uint32_t {
        auto sz = static_cast<uint32_t>(sizeof(T));
        new (mem_.appendForOverwrite(sz).data()) T(elem);
        return sz;
    }
    template <typename T>
    auto allocRange(Range& r, uint32_t other, const char* what, uint32_t n) -> std::span<T> {
        POTASSCO_CHECK_PRE(r.start > other || (r.open() && (start(r, 0u), true)), "%s already frozen", what);
        auto sz  = static_cast<uint32_t>(n * sizeof(T));
        auto sp  = mem_.appendForOverwrite(sz);
        r.end   += sz;
        return std::span{reinterpret_cast<T*>(sp.data()), n};
    }
    template <typename T>
    void appendRange(Range& r, uint32_t other, const char* what, const T& elem) {
        POTASSCO_CHECK_PRE(r.start > other || (r.open() && (start(r, 0u), true)), "%s already frozen", what);
        r.end += append(elem);
    }

    using Buffer = DynamicArray<std::byte>;
    Buffer mem_;
    Range  head_{};
    Range  body_{};
};
///@}

} // namespace Potassco
