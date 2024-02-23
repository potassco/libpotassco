//
// Copyright (c) 2010-2024 Benjamin Kaufmann
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
//
// NOTE: ProgramOptions is inspired by Boost.Program_options
//       see: www.boost.org/libs/program_options
//
#ifndef PROGRAM_OPTIONS_MAPPED_VALUE_H_INCLUDED
#define PROGRAM_OPTIONS_MAPPED_VALUE_H_INCLUDED
#ifdef _MSC_VER
#pragma warning (disable : 4786)
#pragma warning (disable : 4503)
#endif

#include <potassco/program_opts/typed_value.h>

#include <any>
#include <cstddef>
#include <map>
#include <string>

namespace Potassco::ProgramOptions {
///////////////////////////////////////////////////////////////////////////////
// ValueMap
///////////////////////////////////////////////////////////////////////////////
//! Type for storing anonymous values.
/*!
 * Maps option names to their values.
 */
class ValueMap {
public:
	ValueMap() = default;
	~ValueMap() = default;
	ValueMap(const ValueMap&) = delete;
	ValueMap& operator=(const ValueMap&) = delete;
	using value_type = std::any;

	[[nodiscard]] bool   empty() const { return map_.empty(); }
	[[nodiscard]] size_t size()  const { return map_.size(); }
	[[nodiscard]] size_t count(std::string_view name) const { return map_.count(name); }

	const value_type& operator[](std::string_view name) const {
		auto it = map_.find(name);
		if (it == map_.end()) {
			throw UnknownOption("ValueMap", std::string(name));
		}
		return it->second;
	}

	template <typename T>
	const T& get(std::string_view name) const {
		const auto& a = this->operator[](name);
		return std::any_cast<const std::remove_reference_t<T>&>(a);
	}

	value_type& getOrAdd(const std::string& name) {
		return map_[name];
	}

	void erase(std::string_view name) {
		if (auto it = map_.find(name); it != map_.end())
			map_.erase(it);
	}

	void clear() { map_.clear(); }

private:
	using MapType = std::map<std::string, std::any, std::less<>>;
	MapType map_;
};

/*!
 * Creates a value that is created on demand and stored in a given value map.
 *
 * \see OptionGroup::addOptions()
 */
template <class T>
inline Value* store(ValueMap& map, typename detail::Parser<T>::type p = &string_cast<T>) {
	return new TypedValue{[map = &map, parser = p](const std::string& n, const std::string& value){
		auto& x       = map->getOrAdd(n);
		bool wasEmpty = !x.has_value();
		if (wasEmpty)
			x.emplace<T>();
		T& val = std::any_cast<T&>(x);
		auto ok = parser(value, val);
		if (!ok && wasEmpty) {
			map->erase(n);
		}
		return ok;
	}};
}

inline auto flag(ValueMap& map, detail::Parser<bool>::type x = store_true) {
	return store<bool>(map, x)->flag();
}

}
#endif
