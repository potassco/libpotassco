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
#include <potassco/match_basic_types.h>

#include <potassco/error.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <istream>
#include <utility>
namespace Potassco {
#define POTASSCO_UNSUPPORTED(what) POTASSCO_FAIL(Errc::domain_error, what " not supported")

AbstractProgram::~AbstractProgram() = default;
void AbstractProgram::initProgram(bool) {}
void AbstractProgram::beginStep() {}
void AbstractProgram::project(AtomSpan) { POTASSCO_UNSUPPORTED("projection"); }
void AbstractProgram::outputTerm(Id_t, std::string_view) { POTASSCO_UNSUPPORTED("output term"); }
void AbstractProgram::output(Id_t, LitSpan) { POTASSCO_UNSUPPORTED("output term"); }
void AbstractProgram::external(Atom_t, TruthValue) { POTASSCO_UNSUPPORTED("externals"); }
void AbstractProgram::assume(LitSpan) { POTASSCO_UNSUPPORTED("assumptions"); }
void AbstractProgram::heuristic(Atom_t, DomModifier, int, unsigned, LitSpan) {
    POTASSCO_UNSUPPORTED("heuristic directive");
}
void AbstractProgram::acycEdge(int, int, LitSpan) { POTASSCO_UNSUPPORTED("edge directive"); }
void AbstractProgram::theoryTerm(Id_t, int) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::theoryTerm(Id_t, std::string_view) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::theoryTerm(Id_t, int, IdSpan) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::theoryElement(Id_t, IdSpan, LitSpan) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::theoryAtom(Id_t, Id_t, IdSpan) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::theoryAtom(Id_t, Id_t, IdSpan, Id_t, Id_t) { POTASSCO_UNSUPPORTED("theory data"); }
void AbstractProgram::endStep() {}
/////////////////////////////////////////////////////////////////////////////////////////
// BufferedStream
/////////////////////////////////////////////////////////////////////////////////////////
BufferedStream::BufferedStream(std::istream& str) : str_(str), buf_(nullptr), rpos_(0), line_(1) {
    buf_ = new char[alloc_size];
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
            if (peek() == '\n') {
                pop();
            }
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
    if (not str_) {
        return;
    }
    if (upPos && rpos_) {
        // keep last char for unget
        buf_[0] = buf_[rpos_ - 1];
        rpos_   = 1;
    }
    auto n = static_cast<std::streamsize>(alloc_size - (1 + rpos_));
    str_.read(buf_ + rpos_, n);
    auto r          = static_cast<std::size_t>(str_.gcount());
    buf_[r + rpos_] = 0;
}
bool BufferedStream::unget(char c) {
    if (not rpos_) {
        return false;
    }
    if (buf_[--rpos_] = c; c == '\n') {
        --line_;
    }
    return true;
}
bool BufferedStream::match(std::string_view w) {
    if (auto bLen = buf_size - rpos_; bLen < w.length()) {
        POTASSCO_ASSERT(w.length() <= buf_size, "Token too long - Increase BUF_SIZE!");
        std::memcpy(buf_, buf_ + rpos_, bLen);
        rpos_ = bLen;
        underflow(false);
        rpos_ = 0;
    }
    if (std::strncmp(w.data(), buf_ + rpos_, w.length()) == 0) {
        if (rpos_ += w.length(); not buf_[rpos_]) {
            underflow();
        }
        return true;
    }
    return false;
}
bool BufferedStream::readInt(int64_t& res) {
    skipWs();
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
std::size_t BufferedStream::read(std::span<char> outBuf) {
    std::size_t os = 0;
    for (auto n = outBuf.size(); n && peek();) {
        auto  b   = (alloc_size - rpos_) - 1;
        auto  m   = std::min(n, b);
        auto* out = outBuf.data() + os;
        std::copy_n(buf_ + rpos_, m, out);
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
/////////////////////////////////////////////////////////////////////////////////////////
// ProgramReader
/////////////////////////////////////////////////////////////////////////////////////////
ProgramReader::~ProgramReader() { delete str_; }
bool ProgramReader::accept(std::istream& str) {
    reset();
    str_ = new StreamType(str);
    inc_ = false;
    skipWs();
    return doAttach(inc_);
}
bool ProgramReader::incremental() const { return inc_; }
bool ProgramReader::parse(ReadMode r) {
    POTASSCO_CHECK_PRE(str_ != nullptr, "no input stream");
    do {
        if (not doParse()) {
            return false;
        }
        skipWs();
        require(not more() || incremental(), "invalid extra input");
    } while (r == read_complete && more());
    return true;
}
bool ProgramReader::more() { return str_ && (str_->skipWs(), not str_->end()); }
void ProgramReader::reset() {
    doReset();
    delete std::exchange(str_, nullptr);
}
void            ProgramReader::doReset() {}
unsigned        ProgramReader::line() const { return str_ ? str_->line() : 1; }
BufferedStream* ProgramReader::stream() const { return str_; }
void            ProgramReader::error(const char* msg) const {
    POTASSCO_FAIL(std::errc::operation_not_supported, "parse error in line %u: %s", str_->line(), msg);
}
char ProgramReader::get() { return str_->get(); }
char ProgramReader::peek() const { return str_->peek(); }
void ProgramReader::skipLine() {
    while (str_->peek() && str_->get() != '\n') {}
}
char ProgramReader::skipWs() { return str_->skipWs(), str_->peek(); }
void ProgramReader::matchChar(char c) {
    POTASSCO_CHECK(str_->get() == c, std::errc::operation_not_supported, "parse error in line %u: '%c' expected",
                   str_->line(), c);
}
int readProgram(std::istream& str, ProgramReader& reader) {
    if (not reader.accept(str) || not reader.parse(ProgramReader::read_complete)) {
        reader.error("invalid input format");
    }
    return 0;
}
bool matchTerm(std::string_view& input, std::string_view& arg) {
    auto        scan = input;
    std::size_t pos  = 0;
    for (std::size_t end = scan.size(), paren = 0; pos != end; ++pos) {
        if (auto c = scan[pos]; c == '(') {
            ++paren;
        }
        else if (c == ')') {
            if (paren-- == 0) {
                break;
            }
        }
        else if (c == '"') {
            for (auto quoted = false; ++pos != end && ((c = scan[pos]) != '\"' || quoted);) {
                quoted = not quoted && c == '\\';
            }
            if (pos == end) {
                break;
            }
        }
        else if (paren == 0 && c == ',') {
            break;
        }
    }
    arg   = input.substr(0, pos);
    input = scan.substr(pos);
    return not arg.empty();
}
bool matchNum(std::string_view& in, std::string_view* sOut, int* nOut) {
    int  n;
    auto r  = std::from_chars(in.data(), in.data() + in.size(), nOut ? *nOut : n);
    auto sz = static_cast<std::size_t>(r.ptr - in.data());
    if (r.ec != std::errc{} || sz == 0) {
        return false;
    }
    if (sOut) {
        *sOut = in.substr(0, sz);
    }
    in.remove_prefix(sz);
    return true;
}
auto atomView(std::string_view atom) -> AtomView {
    using namespace std::literals;
    AtomView res;
    res.id    = atom.substr(0, atom.find('('));
    auto args = atom.substr(res.id.size());
    if (args.size() < 3 || args.back() != ')') { // zero arity - pred or pred()
        res.arity = 0 - (not args.empty() && args != "()"sv);
        return res;
    }
    res.arity = 1;
    args.remove_prefix(1);
    res.args = args.substr(0, args.size() - 1);
    for (auto t = ""sv; matchTerm(args, t) && args.size() > 2 && args.starts_with(',');) {
        ++res.arity;
        args.remove_prefix(1);
    }
    if (args != ")"sv) {
        res.arity = -1;
        res.args  = {};
    }
    return res;
}
auto predicate(std::string_view atom) -> std::pair<std::string_view, int> {
    auto r = atomView(atom);
    return {r.id, r.arity};
}
auto AtomView::popFront() noexcept -> std::string_view {
    std::string_view popped;
    if (arity >= 1 && matchTerm(args, popped)) {
        args.remove_prefix(args.starts_with(','));
        --arity;
    }
    return popped; // NOLINT
}
auto AtomView::popBack() noexcept -> std::string_view {
    if (arity <= 1) {
        return popFront();
    }
    for (std::size_t pos = args.size(), paren = 0; pos;) {
        switch (auto c = args[--pos]) {
            default: break;
            case '"':
                for (bool quoted = false; pos;) {
                    c = args[--pos];
                    if (c == '"' && not quoted) {
                        break;
                    }
                    quoted = not quoted && c == '\\';
                }
                break;
            case ')': ++paren; break;
            case '(': --paren; break;
            case ',':
                if (paren == 0) {
                    auto popped = args.substr(pos + 1);
                    args.remove_suffix(args.size() - pos);
                    --arity;
                    return popped; // NOLINT
                }
                break;
        }
    }
    return {};
}

auto cmpAtom(std::string_view lhsAtom, std::string_view rhsAtom, AtomCompare cmp) noexcept -> std::strong_ordering {
    if (test(cmp, AtomCompare::cmp_arity)) {
        auto [lId, lArity] = predicate(lhsAtom);
        auto [rId, rArity] = predicate(rhsAtom);
        if (auto res = lArity <=> rArity; res != 0) {
            return res;
        }
        if (auto res = lId <=> rId; res != 0) {
            return res;
        }
        lhsAtom = lhsAtom.substr(lId.size());
        rhsAtom = rhsAtom.substr(rId.size());
    }
    if (test(cmp, AtomCompare::cmp_natural)) {
        for (auto end = std::min(lhsAtom.size(), rhsAtom.size()), x = static_cast<decltype(end)>(0); x != end; ++x) {
            if (auto l = lhsAtom[x], r = rhsAtom[x]; BufferedStream::isDigit(l) && BufferedStream::isDigit(r)) {
                auto lhsStart = lhsAtom.substr(x);
                auto rhsStart = rhsAtom.substr(x);
                int  lhsNum, rhsNum;
                auto skip = std::string_view{};
                matchNum(lhsStart, &skip, &lhsNum);
                matchNum(rhsStart, nullptr, &rhsNum);
                if (lhsNum != rhsNum) {
                    return lhsNum <=> rhsNum;
                }
                x += skip.size() - 1;
            }
            else if (auto res = l <=> r; res != 0) {
                return res;
            }
        }
        return lhsAtom.size() <=> rhsAtom.size();
    }
    return lhsAtom <=> rhsAtom;
}

} // namespace Potassco
