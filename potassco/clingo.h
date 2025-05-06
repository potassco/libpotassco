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

namespace Potassco {

/*!
 * \defgroup Clingo Clingo
 * \brief Interfaces for communicating with a solver.
 */
///@{

//! Supported clause types in theory propagation.
enum class ClauseType : unsigned {
    learnt           = 0u, //!< Cumulative removable (i.e. subject to nogood deletion) clause.
    locked           = 1u, //!< Cumulative unremovable clause.
    transient        = 2u, //!< Removable clause associated with the current solving step.
    transient_locked = 3u  //!< Unremovable clause associated with the current solving step.
};
POTASSCO_ENABLE_BIT_OPS(ClauseType);

//! Represents an assignment of a particular solver.
class AbstractAssignment {
public:
    virtual ~AbstractAssignment();
    //! Returns the number of variables in the assignment.
    [[nodiscard]] virtual uint32_t size() const = 0;
    //! Returns the number of unassigned variables in the assignment.
    [[nodiscard]] virtual uint32_t unassigned() const = 0;
    //! Returns whether the current assignment is conflicting.
    [[nodiscard]] virtual bool hasConflict() const = 0;
    //! Returns the number of decision literals in the assignment.
    [[nodiscard]] virtual uint32_t level() const = 0;
    //! Returns the number of decision literals that will not be backtracked while solving.
    [[nodiscard]] virtual uint32_t rootLevel() const = 0;
    //! Returns whether `lit` is a valid literal in this assignment.
    [[nodiscard]] virtual bool hasLit(Lit_t lit) const = 0;
    //! Returns the truth value currently assigned to `lit` or `TruthValue::free` if `lit` is unassigned.
    [[nodiscard]] virtual TruthValue value(Lit_t lit) const = 0;
    //! Returns the decision level on which `lit` was assigned or `UINT32_MAX` if `lit` is unassigned.
    [[nodiscard]] virtual uint32_t level(Lit_t lit) const = 0;
    //! Returns the decision literal of the given decision level.
    [[nodiscard]] virtual Lit_t decision(uint32_t) const = 0;
    //! Returns the number of literals in the assignment trail.
    [[nodiscard]] virtual uint32_t trailSize() const = 0;
    //! Returns the literal in the trail at the given position.
    /*!
     * \pre <tt>pos \< trailSize()</tt>
     */
    [[nodiscard]] virtual Lit_t trailAt(uint32_t pos) const = 0;
    //! Returns the trail position of the first literal assigned at the given level.
    /*!
     * \pre <tt>level \<= level()</tt>
     */
    [[nodiscard]] virtual uint32_t trailBegin(uint32_t level) const = 0;
    //! Returns the one-past-the-end position of literals assigned at the given decision level.
    /*!
     * \note Literals assigned at the given level are in the half-open range [trailBegin(), trailEnd()).
     * \pre level \<= level()
     */
    [[nodiscard]] uint32_t trailEnd(uint32_t level) const;

    //! Returns whether the current assignment is total.
    /*!
     * The default implementation returns <tt>unassigned() == 0.</tt>
     */
    [[nodiscard]] virtual bool isTotal() const;
    //! Returns whether the given literal is irrevocably assigned on the top level.
    [[nodiscard]] bool isFixed(Lit_t lit) const;
    //! Returns whether the given literal is true wrt the current assignment.
    [[nodiscard]] bool isTrue(Lit_t lit) const;
    //! Returns whether the given literal is false wrt the current assignment.
    [[nodiscard]] bool isFalse(Lit_t lit) const;
};

//! Represents one particular solver instance.
class AbstractSolver {
public:
    virtual ~AbstractSolver();
    //! Returns the id of the solver associated with this object.
    [[nodiscard]] virtual Id_t id() const = 0;
    //! Returns the current assignment of the solver.
    [[nodiscard]] virtual const AbstractAssignment& assignment() const = 0;

    //! Adds the given clause to the solver if possible.
    /*!
     * If the function is called during propagation, the return value
     * indicates whether propagation may continue (true) or shall be
     * aborted (false).
     *
     * \param clause The literals that make up the clause.
     * \param prop   Properties to be associated with the new clause.
     *
     * \note If the given clause contains a volatile variable, i.e., a variable
     * that was created with `Solver::addVariable(),` it is also considered volatile.
     *
     */
    [[nodiscard]] virtual bool addClause(LitSpan clause, ClauseType prop) = 0;
    bool                       addClause(LitSpan clause) { return addClause(clause, ClauseType::learnt); }

    //! Adds a new volatile variable to this solver instance.
    /*!
     * The new variable is volatile, i.e., only valid within the current solving step
     * and only added to this one particular solver instance.
     *
     * \return The positive literal of the new variable.
     */
    [[nodiscard]] virtual Lit_t addVariable() = 0;
    //! Propagates any newly implied literals.
    virtual bool propagate() = 0;

    /*!
     * \name Propagate control
     * \brief Functions that must only be called in the context of a propagator.
     *
     * @{ */

    //! Returns whether the active propagator watches `lit` in this solver instance.
    [[nodiscard]] virtual bool hasWatch(Lit_t lit) const = 0;

    //! Adds the active propagator to the list of propagators to be notified when the given literal is assigned in this
    //! solver instance.
    /*!
     * \post `hasWatch(lit)` returns true.
     */
    virtual void addWatch(Lit_t lit) = 0;
    //! Removes the active propagator from the list of propagators watching `lit` in the given solver.
    /*!
     * \post `hasWatch(lit)` returns false.
     */
    virtual void removeWatch(Lit_t lit) = 0;
    //@}
};

//! Supported check modes for propagators.
enum class PropagatorCheckMode {
    no       = 0u, //!< Never call AbstractPropagator::check().
    total    = 1u, //!< Call AbstractPropagator::check() only on total assignment.
    fixpoint = 2u, //!< Call AbstractPropagator::check() on every propagation fixpoint.
    both     = 3u  //!< Call AbstractPropagator::check() on every fixpoint and total assignment.
};

//! Supported undo modes for propagators.
enum class PropagatorUndoMode {
    def    = 0u, //!< Call AbstractPropagator::undo() only on levels with non-empty changelist.
    always = 1u  //!< Call AbstractPropagator::undo() on all levels that have been propagated or checked.
};

//! Base class for implementing propagators.
class AbstractPropagator {
public:
    //! Type for representing a set of literals that have recently changed.
    using ChangeList = LitSpan;

    //! Interface for initializing a propagator.
    class Init {
    public:
        using CheckMode = PropagatorCheckMode;
        using UndoMode  = PropagatorUndoMode;
        virtual ~Init();
        //! Returns the check mode of the propagator.
        [[nodiscard]] virtual auto checkMode() const -> CheckMode = 0;
        //! Returns the undo mode of the propagator.
        [[nodiscard]] virtual auto undoMode() const -> UndoMode = 0;
        //! Returns the current (top-level) assignment.
        [[nodiscard]] virtual auto assignment() const -> const AbstractAssignment& = 0;
        //! Returns the number of solvers that will be active during solving.
        [[nodiscard]] virtual auto numSolver() const -> uint32_t = 0;
        //! Maps the given program literal to a solver literal.
        [[nodiscard]] virtual auto solverLiteral(Lit_t lit) const -> Lit_t = 0;

        //! Sets the check mode for the propagator.
        virtual void setCheckMode(CheckMode m) = 0;
        //! Sets the undo mode for the propagator.
        /*!
         * \note By default, AbstractPropagator::undo() is only called for levels on which
         *       at least one watched literal has been assigned. However, if `m` is set
         *       to `always`, AbstractPropagator::undo() is also called for levels L with an
         *       empty change list if AbstractPropagator::check() has been called on L.
         */
        virtual void setUndoMode(UndoMode m) = 0;

        //! Adds a watch for the given <b>solver literal</b> to all current and future solvers.
        void addWatch(Lit_t lit);
        //! Adds a watch for the given <b>solver literal</b> to the solver with the given id.
        virtual void addWatch(Lit_t lit, uint32_t solverId) = 0;
        //! Removes the watch for the given <b>solver literal</b> from all solvers.
        void removeWatch(Lit_t lit);
        //! Removes the watch for the given <b>solver literal</b> from the solver with the given id.
        virtual void removeWatch(Lit_t lit, uint32_t solverId) = 0;
        //! Freezes the variable of the given <b>solver literal</b>.
        /*
         * Solver variables that are not frozen are subject to simplification and might be removed in a preprocessing
         * step after propagator initialization. A propagator should freeze all literals over which it might add clauses
         * during propagation.
         * \note Watched literals are automatically frozen.
         */
        virtual void freezeLiteral(Lit_t lit) = 0;
        //! Creates a new <b>solver literal</b>.
        /*!
         * If `freeze` is true, `freezeLiteral()` is implicitly called on the new literal.
         */
        virtual Lit_t addLiteral(bool freeze) = 0;
        //! Adds a clause over the given <b>solver literals</b>.
        /*!
         * \return false if the program became unsatisfiable
         */
        virtual bool addClause(LitSpan clause) = 0;
        //! Adds a weight constraint over the given <b>solver literals</b>.
        /*!
         * Adds a constraint of form `con <=> { l=w | (l, w) in lits } ?= bound`, where:
         *  - <=> is a left implication if `type` < 0,
         *  - <=> is a right implication if `type` > 0,
         *  - <=> is an equivalence if `type` = 0, and
         *  - ?= is `>=` if `eq` is false and `==`, otherwise.
         *
         * \return false if the program became unsatisfiable
         */
        virtual bool addWeightConstraint(Lit_t con, WeightLitSpan lits, Weight_t bound, int32_t type, bool eq) = 0;
        //! Adds a weak constraint over the given <b>solver literals</b>.
        virtual void addMinimize(Weight_t prio, WeightLit lit) = 0;
        //! Propagates the consequences of the underlying problem excluding any registered propagators.
        /*!
         * \return false if the program becomes unsatisfiable.
         */
        virtual bool propagate() = 0;
    };

    virtual ~AbstractPropagator();
    //! Called before solving to initialize the propagator.
    virtual void init(Init& init) = 0;
    //! Shall propagate the newly assigned literals given in `changes`.
    virtual void propagate(AbstractSolver& solver, LitSpan changes) = 0;
    //! May update the internal state of the newly unassigned literals given in `undo`.
    virtual void undo(const AbstractSolver& solver, LitSpan undo) = 0;
    //! Similar to propagate but called on an assignment without a list of changes.
    virtual void check(AbstractSolver& solver) = 0;
};

//! Base class for implementing heuristics.
class AbstractHeuristic {
public:
    virtual ~AbstractHeuristic();
    //! Shall return the literal that the solver with the given id should decide on next.
    /*!
     * \param solverId The id of an active solver.
     * \param assignment The current assignment of the solver with the given id.
     * \param fallback A literal that the active solver selected as its next decision literal.
     * \pre fallback is a valid decision literal, i.e., it is not yet assigned.
     * \return A literal to decide on next.
     *
     * \note If the function returns 0 or a literal that is already assigned, the returned lit
     *       is implicitly replaced with fallback.
     */
    virtual Lit_t decide(Id_t solverId, const AbstractAssignment& assignment, Lit_t fallback) = 0;
};

//! Supported (solver) statistics types.
enum class StatisticsType {
    value = 0, //!< Single statistic value that is convertible to a double.
    array = 1, //!< Composite object mapping int keys to statistics types.
    map   = 2  //!< Composite object mapping string keys to statistics types.
};

//! Base class for providing (solver) statistics.
/*!
 * Functions in this interface taking a key as a parameter
 * assume that the key is valid and throw a std::logic_error
 * if this assumption is violated.
 */
class AbstractStatistics {
public:
    //! Opaque type for representing (sub) keys.
    using Key_t = uint64_t;
    using Type  = StatisticsType;

    virtual ~AbstractStatistics();

    //! Returns the root key of this statistic object.
    [[nodiscard]] virtual Key_t root() const = 0;
    //! Returns the type of the object with the given key.
    [[nodiscard]] virtual Type type(Key_t key) const = 0;
    //! Returns the child count of the object with the given key or 0 if it is a value.
    [[nodiscard]] virtual size_t size(Key_t key) const = 0;
    //! Returns whether the object with the given key can be updated.
    [[nodiscard]] virtual bool writable(Key_t key) const = 0;

    /*!
     * \name Array
     * Functions in this group shall only be called on StatisticsType::array objects.
     */
    //@{
    //! Returns the element at the given zero-based index.
    /*!
     * \pre <tt>index \< size(key)</tt>
     */
    [[nodiscard]] virtual Key_t at(Key_t arr, size_t index) const = 0;

    //! Appends a statistic object to the end of the given array.
    /*!
     * \pre writable(arr).
     * \param arr The array object to which the statistic object should be added.
     * \param type The type of the statistic object to append.
     * \return The key of the created statistic object.
     *
     */
    virtual Key_t push(Key_t arr, Type type) = 0;
    //@}

    /*!
     * \name Map
     * Functions in this group shall only be called on StatisticsType::map objects.
     */
    //@{
    //! Returns the name of the ith element in the given map.
    /*!
     * \pre <tt>i \< size(mapK)</tt>
     * \note The order of elements in a map is unspecified and might change after a solve operation.
     */
    [[nodiscard]] virtual std::string_view key(Key_t mapK, size_t i) const = 0;

    //! Returns the element stored in the map under the given name.
    [[nodiscard]] virtual Key_t get(Key_t mapK, std::string_view at) const = 0;

    //! Searches the given map for an element.
    /*!
     * \param mapK    The map object to search.
     * \param element The element to search for.
     * \param outKey  An optional out parameter for storing the key of the element if found.
     * \return Whether the element was found.
     * \post !find(mapK, element, outKey) || !outKey || *outKey == get(mapK, element).
     */
    [[nodiscard]] virtual bool find(Key_t mapK, std::string_view element, Key_t* outKey) const = 0;

    //! Creates a statistic object under the given name in the given map.
    /*!
     * \pre `writable(mapK)`.
     * \param mapK The map object to which the statistic object should be added.
     * \param name The name under which the statistic object should be added.
     * \param type The type of the statistic object to create.
     * \return The key of the added statistic object.
     *
     * \note If a statistic object with the given name already exists in the map,
     *       the function either returns its key provided that the types match,
     *       or otherwise signals failure by throwing a std::logic_error.
     */
    virtual Key_t add(Key_t mapK, std::string_view name, Type type) = 0;
    //@}
    /*!
     * \name Value
     * Functions in this group shall only be called on StatisticsType::value objects.
     */
    //@{
    //! Returns the statistic value associated with the given key.
    [[nodiscard]] virtual double value(Key_t key) const = 0;

    //! Sets value as value for the given statistic object.
    /*!
     * \pre `writable(key)`.
     */
    virtual void set(Key_t key, double value) = 0;
    //@}
};
///@}

} // namespace Potassco
