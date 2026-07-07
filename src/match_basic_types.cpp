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
#include <potassco/utils.h>

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
BufferedStream::BufferedStream(std::istream& str) : str_(str), buf_(new char[buf_size + 1]) { underflow(0); }
BufferedStream::~BufferedStream() { delete[] buf_; }
auto BufferedStream::avail() const -> uint32_t { return rEnd_ - rpos_; }
void BufferedStream::advance(uint32_t n) {
    POTASSCO_DEBUG_ASSERT(rpos_ + n <= rEnd_);
    if (rpos_ += n; not buf_[rpos_]) {
        underflow(1); // keep space for unget
    }
}
char BufferedStream::pop() {
    auto c = peek();
    advance(1);
    return c;
}
char BufferedStream::get() {
    if (auto c = peek(); c) {
        advance(1);
        if (c == cr) {
            if (peek() == nl) {
                advance(1);
            }
            c = nl;
        }
        line_ += (c == nl);
        return c;
    }
    return 0;
}
void BufferedStream::skipWs() {
    for (char c; (c = peek()) >= 9 && c < 33;) { get(); }
}

static constexpr auto findNewLine(const char* in) -> uint32_t {
    for (const char* r = in;;) {
        while (*r > '\r') { ++r; }
        if ((0x2401u >> *r) & 1u) {
            return static_cast<uint32_t>(r - in);
        }
        ++r;
    }
}

void BufferedStream::skipLine() {
    for (;;) {
        advance(findNewLine(buf_ + rpos_));
        if (auto n = get(); n == nl || n == 0) {
            return;
        }
    }
}

void BufferedStream::underflow(uint32_t pos) {
    if (not str_) {
        buf_[rEnd_ = rpos_ = pos] = 0;
        return;
    }
    str_.read(buf_ + pos, static_cast<std::streamsize>(buf_size - pos));
    rpos_ = pos;
    rEnd_ = pos + static_cast<uint32_t>(str_.gcount());
    POTASSCO_ASSERT(rEnd_ <= buf_size);
    buf_[rEnd_] = 0;
}
bool BufferedStream::unget(char c) {
    if (not rpos_) {
        return false;
    }
    if (buf_[--rpos_] = c; c == nl) {
        --line_;
    }
    return true;
}
bool BufferedStream::match(std::string_view tok) {
    if (auto bLen = avail(); bLen < tok.length()) {
        POTASSCO_ASSERT(tok.length() <= buf_size, "Token too long - Increase BUF_SIZE!");
        std::memcpy(buf_, buf_ + rpos_, bLen);
        underflow(bLen);
        rpos_ = 0;
    }
    if (std::strncmp(tok.data(), buf_ + rpos_, tok.length()) == 0) {
        advance(static_cast<uint32_t>(tok.length()));
        return true;
    }
    return false;
}
bool BufferedStream::readInt(int64_t& res) {
    skipWs();
    auto s = peek();
    if (s == '+' || s == '-') {
        advance(1);
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
auto BufferedStream::read(std::span<char> bufferOut) -> std::size_t {
    auto* out = bufferOut.data();
    for (auto n = bufferOut.size(); n && peek();) {
        auto m  = std::min<std::size_t>(n, avail());
        out     = std::copy_n(buf_ + rpos_, m, out);
        n      -= m;
        advance(static_cast<uint32_t>(m));
    }
    return static_cast<std::size_t>(out - bufferOut.data());
}
auto BufferedStream::readLine(DynamicBuffer& bufferOut) -> std::size_t {
    for (auto sz = bufferOut.size();;) {
        const char* in   = buf_ + rpos_;
        const auto  read = findNewLine(in);
        bufferOut.append(in, read);
        advance(read);
        auto n = get();
        if (n == nl || n == 0) {
            return bufferOut.size() - sz;
        }
        bufferOut.push(n);
    }
}
auto BufferedStream::line() const -> unsigned { return line_; }
/////////////////////////////////////////////////////////////////////////////////////////
// ProgramReader
/////////////////////////////////////////////////////////////////////////////////////////
ProgramReader::~ProgramReader() = default;
bool ProgramReader::accept(std::istream& str) {
    reset();
    str_ = std::make_unique<StreamType>(str);
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
    str_.reset();
}
void ProgramReader::doReset() {}
auto ProgramReader::line() const -> unsigned { return str_ ? str_->line() : 1; }
auto ProgramReader::stream() const -> BufferedStream* { return str_.get(); }
void ProgramReader::error(const char* msg) const {
    POTASSCO_FAIL(std::errc::operation_not_supported, "parse error in line %u: %s", str_->line(), msg);
}
char ProgramReader::get() { return str_->get(); }
char ProgramReader::peek() const { return str_->peek(); }
void ProgramReader::skipLine() { return str_->skipLine(); }
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
bool matchTerm(std::string_view& input, std::string_view& termOut) {
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
    termOut = input.substr(0, pos);
    input   = scan.substr(pos);
    return not termOut.empty();
}
static constexpr auto arg(std::string_view arg, AtomArgMode mode) -> std::string_view {
    if (mode == AtomArgMode::raw || not arg.starts_with('"') || not arg.ends_with('"')) {
        return arg; // NOLINT
    }
    return arg.substr(1, arg.size() - 2);
}
auto popArg(std::string_view& args, AtomArg argPos, AtomArgMode mode) -> std::string_view {
    if (argPos == AtomArg::first) {
        if (std::string_view matched; matchTerm(args, matched)) {
            args.remove_prefix(args.starts_with(','));
            return arg(matched, mode);
        }
        return {};
    }
    auto pos = args.size();
    for (auto paren = 0; pos && (args[--pos] != ',' || paren > 0);) {
        switch (auto c = args[pos]) {
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
        }
    }
    auto matched = args.substr(pos);
    args.remove_suffix(matched.size());
    matched.remove_prefix(pos && matched.starts_with(','));
    return arg(matched, mode);
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
auto atomSymbol(std::string_view atom) -> std::tuple<std::string_view, int, std::string_view> {
    using namespace std::literals;
    auto id   = atom.substr(0, atom.find('('));
    auto args = atom.substr(id.size());
    if (args.size() < 3 || args.back() != ')') { // zero arity - pred or pred()
        int arity = 0 - (not args.empty() && args != "()"sv);
        return {id, arity, {}};
    }
    int arity = 1;
    args.remove_prefix(1);
    auto outArgs = args.substr(0, args.size() - 1);
    for (auto t = ""sv; matchTerm(args, t) && args.size() > 2 && args.starts_with(',');) {
        ++arity;
        args.remove_prefix(1);
    }
    if (args != ")"sv) {
        arity   = -1;
        outArgs = {};
    }
    return {id, arity, outArgs};
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
        for (std::size_t ls = lhsAtom.size(), rs = rhsAtom.size(), end = std::min(ls, rs), x = 0; x < end;) {
            if (auto l = lhsAtom[x], r = rhsAtom[x]; isDigit(l) && isDigit(r)) {
                auto lhs = lhsAtom.substr(std::min(ls, lhsAtom.find_first_not_of('0', x)));
                auto rhs = rhsAtom.substr(std::min(rs, rhsAtom.find_first_not_of('0', x)));
                auto res = std::strong_ordering::equal;
                ls = lhs.size(), rs = rhs.size();
                for (std::size_t lp = 0, rp = 0;;) {
                    l = lp < ls ? lhs[lp++] : 0;
                    r = rp < rs ? rhs[rp++] : 0;
                    if (auto ld = isDigit(l), rd = isDigit(r); not ld || not rd) {
                        if (rd) {
                            res = not ld ? std::strong_ordering::less : res;
                        }
                        else {
                            res = ld ? std::strong_ordering::greater : res;
                        }
                        if (not std::is_eq(res)) {
                            return x == 0 || lhsAtom[x - 1] != '-' ? res : 0 <=> res;
                        }
                        ls  = (lhsAtom = lhs.substr(lp)).size();
                        rs  = (rhsAtom = rhs.substr(rp)).size();
                        end = std::min(ls, rs);
                        break;
                    }
                    if (res == std::strong_ordering::equal) {
                        res = l <=> r;
                    }
                }
            }
            else if (auto res = l <=> r; not std::is_eq(res)) {
                return res;
            }
            else {
                ++x;
            }
        }
        return lhsAtom.size() <=> rhsAtom.size();
    }
    return lhsAtom <=> rhsAtom;
}

} // namespace Potassco
