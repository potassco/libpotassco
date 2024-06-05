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

#include <climits>
#include <cstdint>
#include <ios>
#include <iosfwd>
#include <stdexcept>

namespace Potassco {

/*!
 * \addtogroup ParseType
 */
///@{

//! A wrapper around an std::istream that provides buffering and a simple interface for extracting characters and
//! integers.
class BufferedStream {
public:
    static constexpr auto BUF_SIZE = std::streamsize(4096);
    //! Returns whether the given character is a decimal digit.
    static constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
    //! Converts the given character to a decimal digit.
    static constexpr int toDigit(char c) { return static_cast<int>(c - '0'); }

    //! Creates a new object wrapping the given stream.
    explicit BufferedStream(std::istream& str);
    ~BufferedStream();
    BufferedStream(BufferedStream&&) = delete;

    //! Returns the next character in the input stream, without extracting it.
    [[nodiscard]] char peek() const { return buf_[rpos_]; }
    //! Returns whether the end of the input stream was reached.
    [[nodiscard]] bool end() const { return peek() == 0; }
    //! Extracts the next character from the input stream or returns 0 if the end was reached.
    char get();
    //! Attempts to put back the given character into the read buffer.
    bool unget(char c);
    //! Attempts to read a signed integer from the input stream skipping initial whitespace.
    bool readInt(int64_t&);
    //! Attempts to extract the given string from the input stream.
    /*!
     * If the function returns false, no characters were extracted from the stream.
     * \pre tok.length() <= BUF_SIZE
     */
    bool match(std::string_view tok);
    //! Discards leading whitespace from the input stream.
    void skipWs();
    //! Extracts up to `bufferOut.size()` characters from the input stream and copies them into the given buffer.
    /*!
     * \return The number of characters copied to the given buffer.
     */
    std::size_t read(std::span<char> bufferOut);
    //! Returns the current line number in the input stream, i.e. the number of '\n' characters extracted so far.
    [[nodiscard]] unsigned line() const;

private:
    static constexpr auto ALLOC_SIZE = BUF_SIZE + 1;
    using BufferType                 = char*;

    char pop();
    void underflow(bool up = true);

    std::istream& str_;
    BufferType    buf_;
    std::size_t   rpos_;
    unsigned      line_;
};

//! Base class for input parsers.
class ProgramReader {
public:
    //! Enumeration type for supported read modes.
    enum ReadMode { Incremental, Complete };
    //! Creates a reader that is not yet associated with any input stream.
    ProgramReader() = default;
    virtual ~ProgramReader();
    ProgramReader(ProgramReader&&) = delete;

    //! Associates the reader with the given input stream and returns whether the stream has the right format.
    bool accept(std::istream& str);
    //! Returns whether the input stream represents an incremental program.
    [[nodiscard]] bool incremental() const;
    //! Attempts to parse the previously accepted input stream.
    /*!
     * Depending on the given read mode, the function either parses the complete program
     * or only the next incremental step.
     */
    bool parse(ReadMode r = Incremental);
    //! Returns whether the input stream has more data or is exhausted.
    bool more();
    //! Resets this object to the state after default construction.
    void reset();
    //! Returns the current line number in the input stream.
    [[nodiscard]] unsigned line() const;
    //! Sets the largest possible variable number.
    /*!
     * The given value is used when matching atoms or literals.
     * If a larger value is found in the input stream, an std::exception is raised.
     */
    void setMaxVar(Atom_t v) { varMax_ = v; }

    //! Unconditionally throws an std::exception with the current line and given message.
    void error(const char* msg) const;

protected:
    using StreamType = BufferedStream;
    using WLit_t     = WeightLit_t;
    //! Shall return true if the format of the input stream is supported by this object.
    /*!
     * \param[out] inc Whether the input stream represents an incremental program.
     */
    virtual bool doAttach(bool& inc) = 0;
    //! Shall parse the next program step.
    virtual bool doParse() = 0;
    //! Shall reset any parsing state.
    virtual void doReset();
    //! Returns the associated input stream.
    [[nodiscard]] StreamType* stream() const;
    //! Extracts and discards characters up to and including the next newline.
    void skipLine();
    //! Extracts and discards any leading whitespace and then returns peek().
    char skipWs();
    //! Returns the next character in the input stream, without extracting it.
    [[nodiscard]] char peek() const;
    //! Returns the next character in the input stream.
    char get();
    //! Throws an std::exception with the current line and given message if cnd is false.
    bool require(bool cnd, const char* msg) const { return cnd || (error(msg), false); }
    //! Attempts to match the given string.
    bool match(const std::string_view& word) { return stream()->match(word); }
    //! Extracts the given character or fails with an std::exception.
    void matchChar(char c);
    //! Extracts an atom (i.e. a positive integer > 0) or fails with an std::exception.
    Atom_t matchAtom(const char* error = "atom expected") {
        return static_cast<Atom_t>(matchUint(atomMin, varMax_, error));
    }
    //! Extracts an atom or zero or fails with an std::exception.
    Atom_t matchAtomOrZero(const char* error = "atom or zero expected") {
        return static_cast<Atom_t>(matchUint(0u, varMax_, error));
    }
    //! Extracts an id or fails with an std::exception.
    Id_t matchId(const char* error = "id expected") { return static_cast<Id_t>(matchUint(0u, idMax, error)); }
    //! Extracts a literal (i.e. positive or negative atom) or fails with an std::exception.
    Lit_t matchLit(const char* error = "literal expected") {
        auto res = matchInt(-static_cast<Lit_t>(varMax_), static_cast<Lit_t>(varMax_));
        require(res != 0, error);
        return static_cast<Lit_t>(res);
    }
    //! Extracts a weight or fails with an std::exception.
    Weight_t matchWeight(bool requirePositive = false, const char* error = "weight expected") {
        return static_cast<Weight_t>(matchInt(requirePositive ? 0 : INT_MIN, INT_MAX, error));
    }
    //! Extracts a weight literal or fails with an std::exception.
    WLit_t matchWLit(bool requirePositive = false, const char* error = "weight literal expected") {
        return {.lit = matchLit(error), .weight = matchWeight(requirePositive, error)};
    }
    //! Extracts an unsigned integer or fails with an std::exception.
    unsigned matchUint(const char* error = "non-negative integer expected") { return matchUint(0u, UINT_MAX, error); }
    //! Extracts a signed integer or fails with an std::exception.
    int matchInt(const char* err = "integer expected") { return matchInt(INT_MIN, INT_MAX, err); }

    //! Extracts an unsigned integer in the range [minV, maxV] or fails with an std::exception.
    unsigned matchUint(unsigned minV, unsigned maxV, const char* error = "non-negative integer expected") {
        return static_cast<unsigned>(matchNum(minV, maxV, error));
    }
    //! Extracts a signed integer in the range [minV, maxV] or fails with an std::exception.
    int matchInt(int minV, int maxV, const char* error = "integer expected") {
        return static_cast<int>(matchNum(minV, maxV, error));
    }

private:
    template <std::integral T>
    [[nodiscard]] int64_t matchNum(T min, T max, const char* err) {
        int64_t n;
        require(stream()->readInt(n) && n >= static_cast<int64_t>(min) && n <= static_cast<int64_t>(max), err);
        return n;
    }
    StreamType* str_    = nullptr;
    Atom_t      varMax_ = atomMax;
    bool        inc_    = false;
};

bool matchTerm(std::string_view& input, std::string_view& termOut);

//! Attaches the given stream to r and calls ProgramReader::parse() with the read mode set to ProgramReader::Complete.
int readProgram(std::istream& str, ProgramReader& r);

} // namespace Potassco
///@}
