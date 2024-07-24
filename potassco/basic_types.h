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

/*!
 * \mainpage notitle
 * A small library for parsing and converting logic programs in aspif and smodels format.
 *
 * The library contains parsers and writers for aspif and smodels format
 * as well as functions and types for converting between the two formats
 * to the extent possible.
 *
 * A specification of aspif can be found in Appendix A of:
 * https://www.cs.uni-potsdam.de/wv/publications/DBLP_conf/iclp/GebserKKOSW16x.pdf
 */
#include <potassco/platform.h>

#include <potassco/bits.h>
#include <potassco/enum.h>

#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#define LIB_POTASSCO_VERSION_MAJOR 2
#define LIB_POTASSCO_VERSION_MINOR 0
#define LIB_POTASSCO_VERSION_PATCH 0
#define LIB_POTASSCO_VERSION                                                                                           \
    POTASSCO_STRING(LIB_POTASSCO_VERSION_MAJOR)                                                                        \
    "." POTASSCO_STRING(LIB_POTASSCO_VERSION_MINOR) "." POTASSCO_STRING(LIB_POTASSCO_VERSION_PATCH)

//! Root namespace for all types and functions of libpotassco.
namespace Potassco {

/*!
 * \defgroup WriteType Program writer types
 * \brief Types and functions for writing logic programs.
 */

/*!
 * \defgroup ParseType Program parser types
 * \brief Types and functions for parsing logic programs.
 */

/*!
 * \defgroup BasicTypes Basic Data Types
 * \brief Basic types for working with logic programs.
 */
///@{
//! Ids are non-negative integers in the range [0..idMax].
using Id_t = uint32_t;
//! Maximum value for ids.
constexpr auto idMax = static_cast<Id_t>(-1);
//! Atom ids are positive integers in the range [atomMin..atomMax].
using Atom_t = uint32_t;
//! Minimum value for atom ids (must not be 0).
constexpr auto atomMin = static_cast<Atom_t>(1);
//! Maximum value for atom ids.
constexpr auto atomMax = static_cast<Atom_t>(((1u) << 31) - 1);
//! Literals are signed atoms.
using Lit_t = int32_t;
//! (Literal) weights are integers.
using Weight_t = int32_t;
//! A literal with an associated weight.
struct WeightLit_t {
    Lit_t    lit;    //!< Literal.
    Weight_t weight; //!< Associated weight.

    friend constexpr bool operator==(const WeightLit_t& lhs, const WeightLit_t& rhs) noexcept  = default;
    friend constexpr auto operator<=>(const WeightLit_t& lhs, const WeightLit_t& rhs) noexcept = default;
    friend constexpr auto operator==(const WeightLit_t& lhs, Lit_t rhs) noexcept {
        return lhs.lit == rhs && lhs.weight == 1;
    }
    friend constexpr auto operator<=>(const WeightLit_t& lhs, Lit_t rhs) noexcept {
        return lhs <=> WeightLit_t{.lit = rhs, .weight = 1};
    }
};

using IdSpan        = std::span<const Id_t>;
using AtomSpan      = std::span<const Atom_t>;
using LitSpan       = std::span<const Lit_t>;
using WeightLitSpan = std::span<const WeightLit_t>;

//! Supported rule head types.
enum class Head_t : unsigned { Disjunctive = 0, Choice = 1 };
[[maybe_unused]] consteval auto enable_meta(std::type_identity<Head_t>) { return DefaultEnum<Head_t, 2u>(); }

//! Supported rule body types.
enum class Body_t : unsigned { Normal = 0, Sum = 1, Count = 2 };
[[maybe_unused]] consteval auto enable_meta(std::type_identity<Body_t>) { return DefaultEnum<Body_t, 3u>(); }

//! Type representing an external value.
enum class Value_t : unsigned { Free = 0, True = 1, False = 2, Release = 3 };
[[maybe_unused]] consteval auto enable_meta(std::type_identity<Value_t>) {
    using enum Value_t;
    using namespace std::literals;
    return EnumEntries(Free, "free"sv, True, "true"sv, False, "false"sv, Release, "release"sv);
}

//! Supported modifications for domain heuristic.
enum class Heuristic_t : unsigned { Level = 0, Sign = 1, Factor = 2, Init = 3, True = 4, False = 5 };
[[maybe_unused]] consteval auto enable_meta(std::type_identity<Heuristic_t>) {
    using enum Heuristic_t;
    using namespace std::literals;
    return EnumEntries(Level, "level"sv, Sign, "sign"sv, Factor, "factor"sv, Init, "init"sv, True, "true"sv, False,
                       "false"sv);
}
[[maybe_unused]] consteval auto enable_ops(std::type_identity<Heuristic_t>) -> CmpOps;

//! Supported aspif directives.
enum class Directive_t : unsigned {
    End       = 0,
    Rule      = 1,
    Minimize  = 2,
    Project   = 3,
    Output    = 4,
    External  = 5,
    Assume    = 6,
    Heuristic = 7,
    Edge      = 8,
    Theory    = 9,
    Comment   = 10
};
[[maybe_unused]] consteval auto enable_meta(std::type_identity<Directive_t>) { return DefaultEnum<Directive_t, 11u>(); }

//! Basic callback interface for constructing a logic program.
class AbstractProgram {
public:
    virtual ~AbstractProgram();
    //! Called once to prepare for a new logic program.
    virtual void initProgram(bool incremental);
    //! Called once before rules and directives of the current program step are added.
    virtual void beginStep();

    //! Add the given rule to the program.
    virtual void rule(Head_t ht, const AtomSpan& head, const LitSpan& body) = 0;
    //! Add the given sum rule to the program.
    virtual void rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) = 0;
    //! Add the given minimize statement to the program.
    virtual void minimize(Weight_t prio, const WeightLitSpan& lits) = 0;

    /*!
     * \name Advanced
     * Functions for adding advanced constructs.
     * By default, functions in this group throw a std::logic_error()
     * to signal that advanced constructs are not supported.
     */
    //@{
    //! Mark the given list of atoms as projection atoms.
    virtual void project(const AtomSpan& atoms);
    //! Output @c str whenever condition is true in a stable model.
    virtual void output(const std::string_view& str, const LitSpan& condition);
    //! If @c v is not equal to @c Value_t::Release, mark a as external and assume value @c v. Otherwise, treat @c a as
    //! regular atom.
    virtual void external(Atom_t a, Value_t v);
    //! Assume the given literals to true during solving.
    virtual void assume(const LitSpan& lits);
    //! Apply the given heuristic modification to atom @c a whenever condition is true.
    virtual void heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition);
    //! Assume an edge between @c s and @c t whenever condition is true.
    virtual void acycEdge(int s, int t, const LitSpan& condition);
    //@}

    /*!
     * \name Theory data
     * Functions for adding theory statements.
     * By default, all theory function throw a std::logic_error().
     * Note, ids shall be unique within one step.
     */
    //@{
    //! Add a new number term.
    virtual void theoryTerm(Id_t termId, int number);
    //! Add a new symbolic term.
    virtual void theoryTerm(Id_t termId, const std::string_view& name);
    //! Add a new compound (function or tuple) term.
    virtual void theoryTerm(Id_t termId, int cId, const IdSpan& args);
    //! Add a new theory atom element.
    virtual void theoryElement(Id_t elementId, const IdSpan& terms, const LitSpan& cond);
    //! Add a new theory atom consisting of the given elements, which have to be added eventually.
    virtual void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements);
    //! Add a new theory atom with guard and right hand side.
    virtual void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs);
    //@}

    //! Called once after all rules and directives of the current program step were added.
    virtual void endStep();
};

/*!
 * \defgroup BasicFunc Basic functions
 * \brief Additional functions over basic types.
 * \ingroup BasicTypes
 */
///@{
//! Returns whether @c n is a valid atom number (i.e. in the range [atomMin..atomMax])
template <std::integral T>
constexpr bool validAtom(T n) {
    if constexpr (std::is_signed_v<T>) {
        return n >= 0 && validAtom(std::make_unsigned_t<T>(n));
    }
    else {
        return n >= atomMin && n <= atomMax;
    }
}
//! Identity function for atoms.
constexpr Atom_t atom(Atom_t atom) { return atom; }
//! Returns the atom of the given literal.
constexpr Atom_t atom(Lit_t lit) { return static_cast<Atom_t>(lit >= 0 ? lit : -lit); }
//! Returns the atom of the given weight literal.
constexpr Atom_t atom(const WeightLit_t& w) { return atom(w.lit); }
//! Returns the positive literal of the given atom.
constexpr Lit_t lit(Atom_t atom) { return static_cast<Lit_t>(atom); }
//! Identity function for literals.
constexpr Lit_t lit(Lit_t lit) { return lit; }
//! Returns the literal of the given weight literal.
constexpr Lit_t lit(const WeightLit_t& w) { return w.lit; }
//! Returns the negative literal of the given atom.
constexpr Lit_t neg(Atom_t a) { return -lit(a); }
//! Returns the complement of the given literal.
constexpr Lit_t neg(Lit_t lit) { return -lit; }
//! Returns the weight of the given atom, which is always 1.
constexpr Weight_t weight(Atom_t) { return 1; }
//! Returns the weight of the given literal, which is always 1.
constexpr Weight_t weight(Lit_t) { return 1; }
//! Returns the weight of the given weight literal.
constexpr Weight_t weight(const WeightLit_t& w) { return w.weight; }

///@}

//! A (dynamically-sized) buffer of raw memory.
/*!
 * The class manages a (dynamically-sized) buffer of memory obtained by malloc/realloc.
 * It uses a simple geometric scheme when the buffer needs to grow.
 */
class DynamicBuffer {
public:
    //! Creates a buffer with given initial capacity.
    explicit DynamicBuffer(std::size_t initialCap = 0);
    ~DynamicBuffer();
    DynamicBuffer(const DynamicBuffer&);
    DynamicBuffer(DynamicBuffer&&) noexcept;
    DynamicBuffer& operator=(DynamicBuffer&&) noexcept;
    DynamicBuffer& operator=(const DynamicBuffer&);

    //! Returns the maximum size that the buffer may grow to without triggering reallocation.
    [[nodiscard]] uint32_t capacity() const noexcept { return cap_; }
    //! Returns the number of bytes used in this buffer.
    [[nodiscard]] uint32_t size() const noexcept { return size_; }
    //! Returns a pointer to the beginning of the buffer.
    [[nodiscard]] char*            data() const noexcept { return static_cast<char*>(beg_); }
    [[nodiscard]] char*            data(std::size_t pos) const noexcept { return data() + pos; }
    [[nodiscard]] std::string_view view(std::size_t pos = 0, std::size_t n = std::string_view::npos) const {
        return {data() + pos, std::min(n, size() - pos)};
    }

    //! Increases the capacity of the buffer to a value that is greater or equal to @c n.
    void reserve(std::size_t n);

    //! Resizes the buffer to accommodate an additional @c n bytes at the end.
    /*!
     * If the current capacity is not sufficient, this function grows the region by reallocating a new block of memory
     * thereby invalidating all existing references into the region.
     *
     * \post <tt>size() >= n</tt>
     */
    [[nodiscard]] std::span<char> alloc(std::size_t n);
    void                          append(const void* what, std::size_t n);
    //! Appends the given character to the buffer.
    void  push(char c) { append(&c, 1); }
    char& back() { return data()[size() - 1]; }

    //! Reduces the number of used bytes in this region by @c n.
    void pop(std::size_t n) { size_ -= n <= size_ ? static_cast<uint32_t>(n) : size_; }
    //! Reduces the number of used bytes in this region to 0.
    void clear() { size_ = 0; }

    //! Swaps this and other.
    void swap(DynamicBuffer& other) noexcept;

    //! Releases all allocated memory in this region.
    /*!
     * \post <tt>size() == capacity() == 0</tt>
     */
    void release() noexcept;

private:
    void*    beg_;
    uint32_t cap_;
    uint32_t size_;
};
inline void swap(DynamicBuffer& lhs, DynamicBuffer& rhs) { lhs.swap(rhs); }

class RuleBuilder;

//! A trivially relocatable immutable string type with small buffer optimization.
/*!
 * Not all std::string implementations are trivially relocatable. E.g. the SSO implemented in gcc (libstdc++) relies on
 * a pointer referencing a buffer internal to the string, making relocation non-trivial.
 * In contrast, this class uses a SSO implementation that is more similar to the one from libc++.
 */
class ConstString {
public:
    using trivially_relocatable = std::true_type;
    //! Supported creation modes.
    enum CreateMode { Unique, Shared };
    //! Creates an empty string.
    constexpr ConstString() noexcept {
        if (std::is_constant_evaluated()) {
            std::fill(std::begin(storage_) + 1, std::end(storage_), char(0));
        }
        storage_[0]          = 0;
        storage_[c_maxSmall] = static_cast<char>(c_maxSmall);
    }
    //! Creates a string by coping @c n.
    /*!
     * The creation mode determines how further copies are handled. If @c n exceeds the SSO limit and @c m is set to
     * @c CreateMode::Shared, further copies only increase an internal reference count.
     */
    ConstString(std::string_view n, CreateMode m = Unique);
    //! Creates a copy of @c o.
    ConstString(const ConstString& o);
    //! "Steals" the content of @c o.
    constexpr ConstString(ConstString&& o) noexcept {
        if (o.small()) {
            if (std::is_constant_evaluated()) {
                std::copy(std::begin(o.storage_), std::end(o.storage_), storage_);
            }
            else {
                std::memmove(storage_, o.storage_, o.size() + 1);
                storage_[c_maxSmall] = o.storage_[c_maxSmall];
            }
        }
        else {
            new (storage_) Large{*o.large()};
            storage_[c_maxSmall] = o.storage_[c_maxSmall];
        }
        o.storage_[0]          = 0;
        o.storage_[c_maxSmall] = static_cast<char>(c_maxSmall);
    }
    constexpr ~ConstString() {
        if (not small()) {
            release();
        }
    }
    ConstString&           operator=(const ConstString&) = delete;
    constexpr ConstString& operator=(ConstString&& other) noexcept {
        if (this != &other) {
            this->~ConstString();
            new (this) ConstString(std::move(other));
        }
        return *this;
    }

    //! Converts this string to a string_view.
    [[nodiscard]] constexpr explicit operator std::string_view() const { return {c_str(), size()}; }
    //! Returns this string as a null-terminated C string.
    [[nodiscard]] constexpr const char* c_str() const { return small() ? storage_ : large()->str; }
    //! Converts this string to a string_view.
    [[nodiscard]] constexpr std::string_view view() const { return static_cast<std::string_view>(*this); }
    //! Returns the length of this string.
    [[nodiscard]] constexpr std::size_t size() const {
        return small() ? c_maxSmall - static_cast<std::size_t>(storage_[c_maxSmall]) : large()->size;
    }
    //! Returns the character at the given position, which shall be \< @c size().
    [[nodiscard]] constexpr char operator[](std::size_t pos) const { return c_str()[pos]; }

    [[nodiscard]] constexpr bool small() const { return storage_[c_maxSmall] < c_largeTag; }
    [[nodiscard]] constexpr bool shareable() const { return storage_[c_maxSmall] == c_largeTag + Shared; }

    friend bool operator==(const ConstString& lhs, const ConstString& rhs) { return lhs.view() == rhs.view(); }
    friend auto operator<=>(const ConstString& lhs, const ConstString& rhs) { return lhs.view() <=> rhs.view(); }

private:
    static constexpr auto c_maxSmall = 23;
    static constexpr auto c_largeTag = c_maxSmall + 1;
    struct Large {
        char*       str;
        std::size_t size;
    };
    [[nodiscard]] const Large* large() const { return reinterpret_cast<const Large*>(storage_); }
    int32_t                    addRef(int32_t x);
    void                       release();
    alignas(Large) char storage_[c_maxSmall + 1];
};

///@}

} // namespace Potassco
template <>
struct std::hash<Potassco::ConstString> : std::hash<std::string_view> {
    using is_transparent = void;
    using std::hash<std::string_view>::operator();
    std::size_t operator()(const Potassco::ConstString& str) const noexcept { return (*this)(str.view()); }
};
