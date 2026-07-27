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
#include <potassco/clingo.h>

#include <potassco/format.h>

namespace Potassco {
AbstractAssignment::~AbstractAssignment() = default;
AbstractPropagator::~AbstractPropagator() = default;
AbstractHeuristic::~AbstractHeuristic()   = default;
AbstractStatistics::~AbstractStatistics() = default;
AbstractPropagator::Control::~Control()   = default;
auto AbstractAssignment::isTotal() const -> bool { return unassigned() == 0u; }
auto AbstractAssignment::isFixed(Lit_t lit) const -> bool { return value(lit) != TruthValue::free && level(lit) == 0; }
auto AbstractAssignment::isTrue(Lit_t lit) const -> bool { return value(lit) == TruthValue::true_; }
auto AbstractAssignment::isFalse(Lit_t lit) const -> bool { return value(lit) == TruthValue::false_; }
auto AbstractAssignment::trailEnd(uint32_t lev) const -> uint32_t {
    return lev < level() ? trailBegin(lev + 1) : trailSize();
}
template <typename T>
static constexpr auto q(const T& arg) -> Augmented<std::remove_cvref_t<T>> {
    return {"'", arg};
}
template <typename E = std::logic_error, typename... Args>
POTASSCO_ATTR_NORETURN static void throwStats(const Args&... args) {
    BasicCharBuffer buffer;
    buffer.appendSep(" ", "bad stats access:", args...);
    throw E(buffer.c_str());
}
void AbstractStatistics::throwType(StatisticsType expected, StatisticsType got) {
    throwStats(q(enum_name(expected)), "expected but got", q(enum_name(got)));
}
void AbstractStatistics::throwKey(Key_t key) { throwStats("invalid key", q(key)); }
void AbstractStatistics::throwPath(std::string_view path, std::string_view at) {
    if (not path.empty() && not at.empty()) {
        throwStats<std::out_of_range>("invalid key", q(at), "in path", q(path));
    }
    at = at.empty() ? path : at;
    throwStats<std::out_of_range>("invalid key", q(at));
}
void AbstractStatistics::throwWrite(Key_t key, Type type) {
    throwStats("key", q(key), "is not a writable", enum_name(type));
}
void AbstractStatistics::throwRange(std::size_t idx, std::size_t size) {
    throwStats<std::out_of_range>("index", q(idx), "is out of range for object of size", q(size));
}
} // namespace Potassco
