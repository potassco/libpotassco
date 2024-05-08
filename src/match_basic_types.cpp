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
#pragma warning(disable : 4996) // std::copy unsafe
#endif

#include <potassco/match_basic_types.h>

#include <potassco/error.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <istream>
#include <utility>
namespace Potassco {
#define POTASSCO_UNSUPPORTED(msg) POTASSCO_FAIL(Errc::domain_error, msg)

AbstractProgram::~AbstractProgram() = default;
void AbstractProgram::initProgram(bool) {}
void AbstractProgram::beginStep() {}
void AbstractProgram::project(const AtomSpan&) { POTASSCO_UNSUPPORTED("projection directive not supported"); }
void AbstractProgram::output(const std::string_view&, const LitSpan&) {
    POTASSCO_UNSUPPORTED("output directive not supported");
}
void AbstractProgram::external(Atom_t, Value_t) { POTASSCO_UNSUPPORTED("external directive not supported"); }
void AbstractProgram::assume(const LitSpan&) { POTASSCO_UNSUPPORTED("assumption directive not supported"); }
void AbstractProgram::heuristic(Atom_t, Heuristic_t, int, unsigned, const LitSpan&) {
    POTASSCO_UNSUPPORTED("heuristic directive not supported");
}
void AbstractProgram::acycEdge(int, int, const LitSpan&) { POTASSCO_UNSUPPORTED("edge directive not supported"); }
void AbstractProgram::theoryTerm(Id_t, int) { POTASSCO_UNSUPPORTED("theory data not supported"); }
void AbstractProgram::theoryTerm(Id_t, const std::string_view&) { POTASSCO_UNSUPPORTED("theory data not supported"); }
void AbstractProgram::theoryTerm(Id_t, int, const IdSpan&) { POTASSCO_UNSUPPORTED("theory data not supported"); }
void AbstractProgram::theoryElement(Id_t, const IdSpan&, const LitSpan&) {
    POTASSCO_UNSUPPORTED("theory data not supported");
}
void AbstractProgram::theoryAtom(Id_t, Id_t, const IdSpan&) { POTASSCO_UNSUPPORTED("theory data not supported"); }
void AbstractProgram::theoryAtom(Id_t, Id_t, const IdSpan&, Id_t, Id_t) {
    POTASSCO_UNSUPPORTED("theory data not supported");
}
void AbstractProgram::endStep() {}
/////////////////////////////////////////////////////////////////////////////////////////
// BufferedStream
/////////////////////////////////////////////////////////////////////////////////////////
BufferedStream::BufferedStream(std::istream& str) : str_(str), rpos_(0), line_(1) {
    buf_ = new char[ALLOC_SIZE];
    underflow();
}
BufferedStream::~BufferedStream() { delete[] buf_; }
char BufferedStream::pop() {
    auto c = peek();
    if (not buf_[++rpos_]) {
        underflow();
    }
    return c;
}
char BufferedStream::get() {
    if (auto c = peek(); c) {
        pop();
        if (c == '\r') {
            c = '\n';
            if (peek() == '\n')
                pop();
        }
        if (c == '\n') {
            ++line_;
        }
        return c;
    }
    return 0;
}
void BufferedStream::skipWs() {
    for (char c; (c = peek()) >= 9 && c < 33;) { get(); }
}

void BufferedStream::underflow(bool upPos) {
    if (not str_)
        return;
    if (upPos && rpos_) {
        // keep last char for unget
        buf_[0] = buf_[rpos_ - 1];
        rpos_   = 1;
    }
    auto n = static_cast<std::streamsize>(ALLOC_SIZE - (1 + rpos_));
    str_.read(buf_ + rpos_, n);
    auto r          = static_cast<std::size_t>(str_.gcount());
    buf_[r + rpos_] = 0;
}
bool BufferedStream::unget(char c) {
    if (not rpos_)
        return false;
    if ((buf_[--rpos_] = c) == '\n') {
        --line_;
    }
    return true;
}
bool BufferedStream::match(std::string_view w) {
    if (auto bLen = BUF_SIZE - rpos_; bLen < w.length()) {
        POTASSCO_ASSERT(w.length() <= BUF_SIZE, "Token too long - Increase BUF_SIZE!");
        std::memcpy(buf_, buf_ + rpos_, bLen);
        rpos_ = bLen;
        underflow(false);
        rpos_ = 0;
    }
    if (std::strncmp(w.data(), buf_ + rpos_, w.length()) == 0) {
        if (not buf_[rpos_ += w.length()]) {
            underflow();
        }
        return true;
    }
    return false;
}
bool BufferedStream::match(int64_t& res, bool noSkipWs) {
    if (not noSkipWs) {
        skipWs();
    }
    auto s = peek();
    if (s == '+' || s == '-') {
        pop();
    }
    if (not isDigit(peek())) {
        return false;
    }
    for (res = toDigit(pop()); isDigit(peek());) {
        res *= 10;
        res += toDigit(pop());
    }
    if (s == '-') {
        res = -res;
    }
    return true;
}
std::size_t BufferedStream::copy(std::span<char> outBuf) {
    std::size_t os = 0;
    for (auto n = outBuf.size(); n && peek();) {
        auto  b   = (ALLOC_SIZE - rpos_) - 1;
        auto  m   = std::min(n, b);
        char* out = outBuf.data() + os;
        std::copy(buf_ + rpos_, buf_ + rpos_ + m, out);
        n     -= m;
        os    += m;
        rpos_ += m;
        if (not peek()) {
            underflow();
        }
    }
    return os;
}
unsigned BufferedStream::line() const { return line_; }
void     BufferedStream::fail(unsigned line, const char* err) {
    POTASSCO_FAIL(std::errc::operation_not_supported, "parse error in line %u: %s", line, err);
}
/////////////////////////////////////////////////////////////////////////////////////////
// ProgramReader
/////////////////////////////////////////////////////////////////////////////////////////
ProgramReader::~ProgramReader() { delete str_; }
bool ProgramReader::accept(std::istream& str) {
    reset();
    str_ = new StreamType(str);
    inc_ = false;
    return doAttach(inc_);
}
bool ProgramReader::incremental() const { return inc_; }
bool ProgramReader::parse(ReadMode m) {
    POTASSCO_CHECK_PRE(str_ != nullptr, "no input stream");
    do {
        if (not doParse()) {
            return false;
        }
        stream()->skipWs();
        require(not more() || incremental(), "invalid extra input");
    } while (m == Complete && more());
    return true;
}
bool ProgramReader::more() { return str_ && (str_->skipWs(), not str_->end()); }
void ProgramReader::reset() {
    delete str_;
    str_ = nullptr;
    doReset();
}
void            ProgramReader::doReset() {}
unsigned        ProgramReader::line() const { return str_ ? str_->line() : 1; }
BufferedStream* ProgramReader::stream() const { return str_; }
bool            ProgramReader::require(bool cnd, const char* msg) const {
    str_->require(cnd, msg);
    return true;
}
void ProgramReader::error(const char* msg) const { BufferedStream::fail(str_->line(), msg); }
char ProgramReader::peek(bool skipws) const {
    if (skipws)
        str_->skipWs();
    return str_->peek();
}
void ProgramReader::skipLine() {
    while (str_->peek() && str_->get() != '\n') {}
}
int readProgram(std::istream& str, ProgramReader& reader) {
    if (not reader.accept(str) || not reader.parse(ProgramReader::Complete)) {
        BufferedStream::fail(reader.line(), "invalid input format");
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
// String matching
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr std::string_view heuristicPred = "_heuristic(";
using namespace std::literals;

bool match(const char*& input, std::string_view word) {
    if (std::strncmp(input, word.data(), word.length()) == 0) {
        input += word.length();
        return true;
    }
    return false;
}
bool matchAtomArg(const char*& input, std::string_view& arg) {
    const char* scan = input;
    for (auto p = 0; *scan; ++scan) {
        if (*scan == '(') {
            ++p;
        }
        else if (*scan == ')') {
            if (--p < 0) {
                break;
            }
        }
        else if (*scan == ',') {
            if (p == 0) {
                break;
            }
        }
        else if (*scan == '"') {
            auto quoted = false;
            for (++scan; *scan && (*scan != '\"' || quoted); ++scan) { quoted = not quoted && *scan == '\\'; }
            if (!*scan) {
                return false;
            }
        }
    }
    arg   = {input, static_cast<std::size_t>(scan - input)};
    input = scan;
    return not arg.empty();
}

bool match(const char*& input, Heuristic_t& heuType) {
    for (const auto& [k, n] : enum_entries<Heuristic_t>()) {
        if (not n.empty() && *input == n.front() && match(input, n)) {
            heuType = static_cast<Heuristic_t>(k);
            return true;
        }
    }
    return false;
}

bool match(const char*& input, int& out) {
    char* eptr;
    auto  t = std::strtol(input, &eptr, 10);
    if (eptr == input || t < INT_MIN || t > INT_MAX) {
        return false;
    }
    out   = static_cast<int>(t);
    input = eptr;
    return true;
}

int matchDomHeuPred(const char*& in, std::string_view& atom, Heuristic_t& type, int& bias, unsigned& prio) {
    if (not match(in, heuristicPred)) {
        return 0;
    }
    if (not matchAtomArg(in, atom) || not match(in, ","sv)) {
        return -1;
    }
    if (not match(in, type) || not match(in, ","sv)) {
        return -2;
    }
    if (not match(in, bias)) {
        return -3;
    }
    prio = static_cast<unsigned>(bias < 0 ? -bias : bias);
    if (not match(in, ","sv)) {
        return match(in, ")"sv) ? 1 : -3;
    }
    int p;
    if (not match(in, p) || p < 0) {
        return -4;
    }
    prio = static_cast<unsigned>(p);
    return match(in, ")"sv) ? 1 : -4;
}

int matchEdgePred(const char*& in, std::string_view& n0, std::string_view& n1) {
    int sPos, tPos, ePos = -1;
    if (sscanf(in, "_acyc_%*d_%n%*d_%n%*d%n", &sPos, &tPos, &ePos) == 0 && ePos > 0) {
        POTASSCO_CHECK(tPos >= sPos && ePos >= tPos, Errc::invalid_argument);
        n0  = {in + sPos, std::size_t(tPos - sPos) - 1};
        n1  = {in + tPos, std::size_t(ePos - tPos)};
        in += ePos;
        return not n0.empty() && not n1.empty() ? 1 : -1;
    }
    else if (match(in, "_edge("sv)) {
        if (not matchAtomArg(in, n0) || not match(in, ","sv)) {
            return -1;
        }
        if (not matchAtomArg(in, n1) || not match(in, ")"sv)) {
            return -2;
        }
        return 1;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
// DynamicBuffer
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr uint32_t c_fastGrowCap = 0x20000u;
static constexpr uint32_t nextCapacity(uint32_t current) {
    if (current == 0u) {
        return 64u;
    }
    return current <= c_fastGrowCap ? (current * 3 + 1) >> 1 : current << 1u;
}
DynamicBuffer::DynamicBuffer(std::size_t init) : beg_(nullptr), cap_(0), size_(0) { reserve(init); }
DynamicBuffer::DynamicBuffer(DynamicBuffer&& other) noexcept
    : beg_(std::exchange(other.beg_, nullptr))
    , cap_(std::exchange(other.cap_, 0))
    , size_(std::exchange(other.size_, 0)) {}
DynamicBuffer::DynamicBuffer(const DynamicBuffer& other) : DynamicBuffer() { append(other.data(), other.size_); }
DynamicBuffer::~DynamicBuffer() { release(); }
DynamicBuffer& DynamicBuffer::operator=(Potassco::DynamicBuffer&& other) noexcept {
    if (this != &other) {
        DynamicBuffer(std::move(other)).swap(*this);
    }
    return *this;
}
DynamicBuffer& DynamicBuffer::operator=(const Potassco::DynamicBuffer& other) {
    if (this != &other) {
        DynamicBuffer(other).swap(*this);
    }
    return *this;
}
void DynamicBuffer::release() noexcept {
    if (auto p = std::exchange(beg_, nullptr); p) {
        std::free(p);
        cap_ = size_ = 0;
    }
}
void DynamicBuffer::swap(DynamicBuffer& other) noexcept {
    std::swap(beg_, other.beg_);
    std::swap(cap_, other.cap_);
    std::swap(size_, other.size_);
}
void DynamicBuffer::reserve(std::size_t n) {
    if (n > capacity()) {
        auto newCap = std::max(static_cast<std::size_t>(nextCapacity(capacity())), n);
        POTASSCO_CHECK(std::in_range<uint32_t>(newCap), Errc::length_error, "region too large");
        void* t = std::realloc(beg_, newCap);
        POTASSCO_CHECK(t, Errc::bad_alloc);
        beg_ = t;
        cap_ = newCap;
    }
}
std::span<char> DynamicBuffer::alloc(std::size_t n) {
    reserve(size() + n);
    return {data(std::exchange(size_, size_ + n)), n};
}
void DynamicBuffer::append(const void* what, std::size_t n) {
    if (n)
        std::memcpy(alloc(n).data(), what, n);
}
/////////////////////////////////////////////////////////////////////////////////////////
// FixedString
/////////////////////////////////////////////////////////////////////////////////////////
FixedString::FixedString(std::string_view n, CreateMode cm) {
    if (n.size() > c_maxSmall) {
        storage_[c_maxSmall] = c_largeTag + cm;
        auto* buf            = new char[(cm == Shared ? sizeof(std::atomic<int32_t>) : 0) + n.size() + 1];
        if (cm == Shared) {
            new (buf) std::atomic<int32_t>(1);
            buf += sizeof(std::atomic<int32_t>);
        }
        *std::copy(n.begin(), n.end(), buf) = 0;
        new (storage_) Large{.str = buf, .size = n.size()};
    }
    else {
        *std::copy(n.begin(), n.end(), storage_) = 0;
        storage_[c_maxSmall]                     = c_maxSmall - n.size();
    }
    POTASSCO_DEBUG_ASSERT(static_cast<std::string_view>(*this) == n);
}
FixedString::FixedString(const FixedString& o) : FixedString(not o.shareable() ? o.view() : std::string_view{}) {
    if (o.shareable()) {
        POTASSCO_DEBUG_ASSERT(not o.small());
        storage_[c_maxSmall] = o.storage_[c_maxSmall];
        new (storage_) Large{*o.large()};
        addRef(1);
    }
}
void FixedString::release() {
    if (not shareable() || addRef(-1) == 0) {
        delete[] (large()->str - (shareable() * sizeof(std::atomic<int32_t>)));
    }
}
int32_t FixedString::addRef(int32_t x) {
    POTASSCO_DEBUG_ASSERT(shareable());
    auto& r   = *reinterpret_cast<std::atomic<int32_t>*>(large()->str - sizeof(std::atomic<int32_t>));
    return r += x;
}
} // namespace Potassco
