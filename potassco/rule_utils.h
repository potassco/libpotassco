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
    AtomSpan head{};                    //!< Head atoms of the rule.
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
    //! Returns whether the rule has a normal body, i.e. whether the body is a conjunction of literals.
    [[nodiscard]] bool normal() const { return bt == BodyType::normal; }
    //! Returns whether the body of the rule is a sum aggregate.
    [[nodiscard]] bool sum() const { return bt != BodyType::normal; }
};

//! A builder class for creating a rule.
class RuleBuilder {
public:
    using trivially_relocatable = std::true_type; // NOLINT

    RuleBuilder()                         = default;
    RuleBuilder(const RuleBuilder& other) = default;
    RuleBuilder(RuleBuilder&& other) noexcept;
    ~RuleBuilder()                                   = default;
    RuleBuilder& operator=(const RuleBuilder& other) = default;
    RuleBuilder& operator=(RuleBuilder&& other) noexcept;
    void         swap(RuleBuilder& other) noexcept;
    /*!
     * \name Start functions.
     * Functions for starting the definition of a rule's head or body.
     * If the active rule is frozen (i.e. end() was called), the active rule is discarded.
     * \note The body of a rule can be defined before or after its head is defined but definitions
     * of head and body must not be mixed.
     */
    //@{
    //! Start definition of the rule's head, which can be either disjunctive or a choice.
    RuleBuilder& start(HeadType ht = HeadType::disjunctive);
    //! Start definition of a minimize rule. No head allowed.
    RuleBuilder& startMinimize(Weight_t prio);
    //! Start definition of a conjunction to be used as the rule's body.
    RuleBuilder& startBody();
    //! Start definition of a sum aggregate to be used as the rule's body.
    RuleBuilder& startSum(Weight_t bound);
    //! Update lower bound of sum aggregate.
    RuleBuilder& setBound(Weight_t bound);
    //@}

    /*!
     * \name Update functions.
     * Functions for adding elements to the active rule.
     * \note Update functions shall not be called once a rule is frozen.
     * \note Calling an update function implicitly starts the definition of the corresponding rule part.
     */
    //@{
    //! Add given atom to the rule's head.
    RuleBuilder& addHead(Atom_t a);
    //! Add lit to the rule's body.
    RuleBuilder& addGoal(Lit_t lit);
    RuleBuilder& addGoal(WeightLit lit);
    RuleBuilder& addGoal(Lit_t lit, Weight_t w) { return addGoal(WeightLit{.lit = lit, .weight = w}); }
    //@}

    //! Stop definition of rule and add rule to out if given.
    /*!
     * Once @c end() was called, the active rule is considered frozen.
     */
    RuleBuilder& end(AbstractProgram* out = nullptr);
    //! Discard active rule and unfreeze builder.
    RuleBuilder& clear();
    //! Discard body of active rule but keep head if any.
    RuleBuilder& clearBody();
    //! Discard head of active rule but keep body if any.
    RuleBuilder& clearHead();
    //! Weaken active sum aggregate body to a normal body or count aggregate.
    RuleBuilder& weaken(BodyType to, bool resetWeights = true);

    /*!
     * \name Query functions.
     * Functions for accessing parts of the active rule.
     * \note The result of these functions is only valid until the next call to an update function.
     */
    //@{
    [[nodiscard]] auto headType() const -> HeadType;
    [[nodiscard]] auto head() const -> AtomSpan;
    [[nodiscard]] auto isMinimize() const -> bool;
    [[nodiscard]] auto bodyType() const -> BodyType;
    [[nodiscard]] auto body() const -> LitSpan;
    [[nodiscard]] auto bound() const -> Weight_t;
    [[nodiscard]] auto sumLits() const -> std::span<WeightLit>;
    [[nodiscard]] auto findSumLit(Lit_t lit) const -> WeightLit*;
    [[nodiscard]] auto sum() const -> Sum;
    [[nodiscard]] auto rule() const -> Rule;
    [[nodiscard]] auto frozen() const -> bool;
    //@}
private:
    struct Range {
        static constexpr auto  start_bit = 0u;
        static constexpr auto  end_bit   = 1u;
        static constexpr auto  mask      = 3u;
        [[nodiscard]] uint32_t start() const { return clear_mask(start_type, mask); }
        [[nodiscard]] uint32_t end() const { return clear_mask(end_flag, mask); }
        [[nodiscard]] uint32_t type() const { return clear_mask(start_type, ~mask); }
        [[nodiscard]] bool     started() const { return test_bit(end_flag, start_bit); }
        [[nodiscard]] bool     finished() const { return test_bit(end_flag, end_bit); }
        [[nodiscard]] bool     open() const { return not test_any(end_flag, mask); }

        uint32_t start_type = 0; // 4-byte aligned, align-bits = type
        uint32_t end_flag   = 0; // 4-byte aligned, align-bits = flags
    };
    void start(Range& r, uint32_t type, const Weight_t* bound = nullptr);
    void clear(Range& r);
    template <typename T>
    void extend(Range& r, const T& elem, const char* what);

    DynamicBuffer mem_;
    Range         head_{};
    Range         body_{};
};
///@}

} // namespace Potassco
