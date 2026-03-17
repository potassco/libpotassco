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

//! Represents an assignment of a solver.
/*!
 * An assignment is always associated with a solver.
 */
class AbstractAssignment {
public:
    virtual ~AbstractAssignment();
    //! Returns the id of the solver this assignment is associated with.
    [[nodiscard]] virtual Id_t solverId() const = 0;
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

    //! Interface for controlling propagation and/or initialization.
    /*!
     * \note Control functions called during propagation only affect the active solver instance. This
     * applies to watches, clauses, and variables. Furthermore, any variables added are volatile. That is,
     * they are only valid within the current solving step and are removed again before the next step is started.
     */
    class Control {
    public:
        virtual ~Control();

        //! Adds the given clause if possible.
        /*!
         * \note If the function is called during propagation, the return value indicates whether
         *       propagation may continue (true) or shall be aborted (false).
         *
         * \param clause The literals that make up the clause.
         * \param type   Type of the new clause.
         * \return       Whether the clause was successfully added.
         */
        [[nodiscard]] virtual bool addClause(LitSpan clause, ClauseType type) = 0;
        [[nodiscard]] bool         addClause(LitSpan clause) { return addClause(clause, ClauseType::learnt); }
        //! Adds a weight constraint over the given <b>solver literals</b>.
        /*!
         * \note If the function is called during propagation, the return value indicates whether
         *       propagation may continue (true) or shall be aborted (false).
         *
         * Adds a constraint of form `con <=> { l=w | (l, w) in lits } >= bound`, where:
         *  - <=> is a left implication if `type` < 0,
         *  - <=> is a right implication if `type` > 0,
         *  - <=> is an equivalence if `type` = 0.
         *
         * \return Whether the constraint was successfully added.
         */
        [[nodiscard]] virtual bool addWeightConstraint(Lit_t con, WeightLitSpan lits, Weight_t bound, int32_t type) = 0;

        //! Creates a new variable and returns the positive <b>solver literal</b> of the new variable.
        /*!
         * If `freeze` is true, the new variable is frozen, i.e., it is not subject to variable elimination.
         * \note Variables added during propagation are volatile, i.e., they are only valid within the current solving
         *       step.
         */
        [[nodiscard]] virtual Lit_t addVariable(bool freeze) = 0;
        [[nodiscard]] Lit_t         addVariable() { return addVariable(true); }

        //! Propagates any newly implied literals.
        virtual bool propagate() = 0;

        //! Returns whether `lit` is a watched literal.
        [[nodiscard]] virtual bool hasWatch(Lit_t lit) const = 0;
        //! Adds the active propagator to the list of propagators to be notified when the given literal is assigned.
        /*!
         * \post `hasWatch(lit)` returns true.
         */
        virtual void addWatch(Lit_t lit) = 0;
        //! Removes the active propagator from the list of propagators watching `lit`.
        /*!
         * \post `hasWatch(lit)` returns false.
         */
        virtual void removeWatch(Lit_t lit) = 0;
    };

    //! Interface for initializing a propagator.
    /*!
     * \note Control functions called on an `Init` object affect all current and future solver instances. This
     * applies to watches, clauses, and variables. Furthermore, all watched literals are automatically frozen.
     */
    class Init : public Control {
    public:
        using CheckMode = PropagatorCheckMode;
        using UndoMode  = PropagatorUndoMode;
        //! Returns the check mode of the propagator.
        [[nodiscard]] virtual auto checkMode() const -> CheckMode = 0;
        //! Returns the undo mode of the propagator.
        [[nodiscard]] virtual auto undoMode() const -> UndoMode = 0;
        //! Returns the number of solvers active during solving.
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

        //! Freezes the variable of the given <b>solver literal</b>.
        /*
         * Solver variables that are not frozen are subject to simplification and might be removed in a preprocessing
         * step after propagator initialization. A propagator should freeze all literals over which it might add clauses
         * during propagation.
         */
        virtual void freezeVariable(Lit_t lit) = 0;

        //! Adds a weak constraint over the given <b>solver literals</b>.
        virtual void addMinimize(Weight_t prio, WeightLit lit) = 0;
    };

    virtual ~AbstractPropagator();
    //! Called before solving to initialize the propagator.
    /*!
     * \note This function is called once for each solving step before any other function of this interface is called.
     * \note
     *   - Variables added during initialization are permanently added to the problem.
     *   - Watches added/removed during initialization are added to/removed from all current and future solvers.
     *     Furthermore, all watched literals are automatically frozen.
     */
    virtual void init(const AbstractAssignment& assignment, Init& init) = 0;
    //! Called before solving to initialize a particular solving thread.
    /*!
     * \note This function is called once after `init()` for each active solver before any other solver-specific
     *       function is called.
     *
     * \param assignment The current assignment of the solver to be initialized.
     * \param ctrl A control object that can be used to change the state of the solver.
     */
    virtual void attach(const AbstractAssignment& assignment, Control& ctrl) = 0;
    //! Shall propagate the newly assigned literals given in `changes`.
    /*!
     * \note If `ctrl.addClause()` or `ctrl.propagate()` is called during propagation and returns false,
     *       propagation shall be aborted.
     * \param assignment The current assignment of the solver.
     * \param ctrl A control object that can be used to change the state of the solver.
     * \param changes The literals that have been assigned since the last call to this function.
     * \pre `assignment.isTrue(x)` is true for all literals `x` in `changes`.
     */
    virtual void propagate(const AbstractAssignment& assignment, Control& ctrl, LitSpan changes) = 0;
    //! Similar to propagate but called on an assignment without a list of changes.
    /*!
     * \param assignment The current assignment of the solver.
     * \param ctrl A control object that can be used to change the state of the solver.
     */
    virtual void check(const AbstractAssignment& assignment, Control& ctrl) = 0;
    //! May update the internal state of the newly unassigned literals given in `undo`.
    /*!
     * \param assignment The current assignment of the solver.
     * \param undo The literals that will be unassigned.
     */
    virtual void undo(const AbstractAssignment& assignment, LitSpan undo) = 0;
};

//! Base class for implementing heuristics.
class AbstractHeuristic {
public:
    virtual ~AbstractHeuristic();
    //! Shall return the next decision literal for the active solver.
    /*!
     * \param assignment The current assignment of the solver.
     * \param fallback A literal that the active solver selected as its next decision literal.
     * \pre fallback is a valid (unassigned) decision literal.
     * \return A literal to decide on next.
     *
     * \note If the function returns 0 or a literal that is already assigned, the returned lit
     *       is implicitly replaced with fallback.
     */
    virtual Lit_t decide(const AbstractAssignment& assignment, Lit_t fallback) = 0;
};

//! Supported (solver) statistics types.
enum class StatisticsType {
    value = 0, //!< Single statistic value that is convertible to a double.
    array = 1, //!< Composite object mapping int keys to statistics types.
    map   = 2  //!< Composite object mapping string keys to statistics types.
};
POTASSCO_SET_ENUM_ENTRIES(StatisticsType, {value, "value"sv}, {array, "array"sv}, {map, "map"sv});
//! Base class for providing (solver) statistics.
/*!
 * Functions in this interface taking a path as a parameter assume that the path is valid and
 * throw a std::logic_error (or an exception derived from it) if this assumption is violated.
 */
class AbstractStatistics {
public:
    //! Path type for addressing statistics.
    using Path_t = std::string_view;
    using Type   = StatisticsType;
    //! Throws a logic error indicating a statistics type mismatch.
    POTASSCO_ATTR_NORETURN static void throwType(StatisticsType expected, StatisticsType got);
    //! Throws a logic error indicating an invalid statistics path.
    POTASSCO_ATTR_NORETURN static void throwPath(std::string_view path, std::string_view at);
    //! Throws a logic error indicating that the addressed object is not a writable statistics object of the given type.
    POTASSCO_ATTR_NORETURN static void throwWrite(Path_t path, Type type);
    //! Throws a logic error indicating that a given index is out of range for an object with given size.
    POTASSCO_ATTR_NORETURN static void throwRange(std::size_t idx, std::size_t size);

    static auto appendPath(Path_t path, std::string_view element) -> std::string;
    static auto appendPath(Path_t path, size_t idx) -> std::string;

    virtual ~AbstractStatistics();

    //! Returns the root path of this statistic object.
    [[nodiscard]] virtual Path_t root() const = 0;
    //! Returns the type of the object under the given path.
    [[nodiscard]] virtual Type type(Path_t path) const = 0;
    //! Returns the child count of the object under the given path or 0 if it is a value.
    [[nodiscard]] virtual size_t size(Path_t path) const = 0;
    //! Returns whether the object under the given path can be updated.
    [[nodiscard]] virtual bool writable(Path_t path) const = 0;

    /*!
     * \name Array
     * Functions in this group shall only be called on StatisticsType::array objects.
     */
    //@{

    //! Appends a statistic object to the end of the given array.
    /*!
     * \pre writable(arr).
     * \param arr The array object to which the statistic object should be added.
     * \param type The type of the statistic object to append.
     * \return The index of the newly added object.
     */
    virtual size_t push(Path_t arr, Type type) = 0;
    //@}

    /*!
     * \name Map
     * Functions in this group shall only be called on StatisticsType::map objects.
     */
    //@{
    //! Returns the name of the ith element in the given map.
    /*!
     * \pre <tt>i \< size(map)</tt>
     * \note The order of elements in a map is unspecified and might change after a solve operation.
     */
    [[nodiscard]] virtual std::string_view key(Path_t map, size_t i) const = 0;

    //! Searches the given map for an element.
    /*!
     * \param map     The map object to search.
     * \param element The element to search for.
     * \return Whether the element was found.
     */
    [[nodiscard]] virtual bool find(Path_t map, std::string_view element) const = 0;

    //! Creates a statistic object under the given name in the given map.
    /*!
     * \pre `writable(map)`.
     * \param map  The map object to which the statistic object should be added.
     * \param name The name under which the statistic object should be added.
     * \param type The type of the statistic object to create.
     * \return Whether a new element was added.
     *
     * \note If a statistic object with the given name already exists in the map,
     *       the behavior depends on the type of the existing object. If the types match,
     *       the function returns false. Otherwise, the function fails by throwing
     *       a `std::logic_error`.
     */
    virtual bool add(Path_t map, std::string_view name, Type type) = 0;
    //@}
    /*!
     * \name Value
     * Functions in this group shall only be called on StatisticsType::value objects.
     */
    //@{
    //! Returns the statistic value under the given path.
    [[nodiscard]] virtual double value(Path_t value) const = 0;

    //! Sets value as value for the statistic object under the given path.
    /*!
     * \pre `writable(key)`.
     */
    virtual void set(Path_t val, double value) = 0;
    //@}
};
///@}

} // namespace Potassco
