//
// Copyright (c) 2017 - present, Benjamin Kaufmann
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
#include <potassco/program_opts/errors.h>
#include <potassco/program_opts/program_options.h>
#include <potassco/program_opts/typed_value.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace Potassco::ProgramOptions::Test {
namespace Po = ProgramOptions;

TEST_CASE("Test intrusive pointer", "[options]") {
    int count = 0;
    struct Foo : Po::Detail::RefCountable {
        constexpr explicit Foo(int& c) : count(&c) { ++*count; }
        constexpr ~Foo() { --*count; }
        Foo(const Foo&)            = delete;
        Foo& operator=(const Foo&) = delete;
        int  x                     = 12;
        int* count                 = nullptr;
    };

    Po::Detail::IntrusiveSharedPtr<Foo> ptr(new Foo(count));
    CHECK(ptr.count() == 1);
    CHECK(count == 1);

    // Disable bogus gcc use-after-free warning with -O2 (or above) in following section.
    POTASSCO_WARNING_PUSH()
    POTASSCO_WARNING_IGNORE_GCC("-Wuse-after-free")
    SECTION("copy") {
        auto ptr2 = ptr;
        CHECK(ptr2.count() == 2);
        Po::Detail::IntrusiveSharedPtr<Foo> ptr3;
        ptr3 = ptr2;
        CHECK(ptr3.count() == 3);
        ptr2 = ptr3;
        CHECK(ptr2.count() == 3);
        ptr2->x = 77;
    }
    POTASSCO_WARNING_POP()

    SECTION("move") {
        auto ptr2 = std::move(ptr);
        CHECK(ptr2.count() == 1);
        CHECK(ptr.get() == nullptr);
        Po::Detail::IntrusiveSharedPtr<Foo> ptr3;
        ptr3 = std::move(ptr2);
        CHECK(ptr3.count() == 1);
        CHECK(ptr2.get() == nullptr);
        ptr = std::move(ptr3);
        CHECK(ptr.get() != nullptr);
        ptr->x = 77;
    }
    REQUIRE(count == 1);
    CHECK(ptr->x == 77);
    ptr.reset();
    CHECK(count == 0);
}

TEST_CASE("Test option default value", "[options]") {
    int x;
    SECTION("options don't have defaults by default") {
        Po::Option o("other-int", 'i', "some other integer", Po::storeTo(x)->arg("<n>"));
        REQUIRE(o.value()->defaultsTo() == static_cast<const char*>(nullptr));
    }
    SECTION("options can have default values") {
        Po::Option o("some-int", 'i', "some integer", Po::storeTo(x)->defaultsTo("123")->arg("<n>"));
        REQUIRE(strcmp(o.value()->defaultsTo(), "123") == 0);
        REQUIRE(strcmp(o.argName(), "<n>") == 0);
        REQUIRE(o.assignDefault());
        REQUIRE(x == 123);
        REQUIRE(o.value()->state() == Po::Value::value_defaulted);
    }
    SECTION("careful with invalid default values") {
        Po::Option o("other-int", 'i', "some other integer", Po::storeTo(x)->defaultsTo("123Hallo?")->arg("<n>"));
        REQUIRE(not o.assignDefault());
        REQUIRE(o.value()->state() == Po::Value::value_unassigned);
    }
    SECTION("parsing overwrites default value") {
        Po::OptionGroup g;
        g.addOptions()("int", Po::storeTo(x)->defaultsTo("10"), "An int");
        Po::OptionContext ctx;
        ctx.add(g);
        Po::ParsedOptions po;
        ctx.assignDefaults(po);
        REQUIRE(x == 10);
        Po::ParsedValues pv(ctx);
        pv.add("int", "2");
        po.assign(pv);
        REQUIRE(x == 2);
    }
}
static bool negatable_int(const std::string& s, int& out) {
    if (s == "no") {
        out = 0;
        return true;
    }
    return Potassco::stringTo(s, out) == std::errc{};
}

TEST_CASE("Test negatable options", "[options]") {
    bool              b1, b2;
    Po::OptionGroup   g;
    Po::OptionContext ctx;
    SECTION("options are not negatable by default") {
        Po::Option o("flag", 'f', "some flag", Po::flag(b1));
        REQUIRE(o.value()->isNegatable() == false);
    }
    SECTION("exclamation mark ('!') in init helper makes option negatable") {
        g.addOptions()("flag!,f", Po::flag(b1), "some flag");
        REQUIRE((*g.begin())->value()->isNegatable() == true);
        ctx.add(g);
        REQUIRE(ctx.tryFind("flag!") == ctx.end());
    }
    SECTION("exclamation mark ('!') can be quoted") {
        g.addOptions()("flag\\!,f", Po::flag(b1), "some flag");
        REQUIRE((*g.begin())->value()->isNegatable() == false);
        ctx.add(g);
        REQUIRE(ctx.tryFind("flag!") != ctx.end());
    }
    SECTION("negatable options are shown in description") {
        int i;
        g.addOptions()("flag!,f", Po::flag(b1), "some negatable flag")(
            "value!", Po::storeTo(i, &negatable_int)->arg("<n>"), "some negatable int");
        ctx.add(g);
        std::string       help;
        Po::OptionPrinter out(help);
        ctx.description(out);
        REQUIRE(help.find("[no-]flag") != std::string::npos);
        REQUIRE(help.find("<n>|no") != std::string::npos);
    }
    SECTION("negatable options are correctly parsed") {
        int i = 123;
        g.addOptions()("flag!,f", Po::flag(b1), "some negatable flag")("flag\\!", Po::flag(b2), "some flag")(
            "value!", Po::storeTo(i, &negatable_int)->arg("<n>"), "some negatable int");
        ctx.add(g);
        Po::ParsedOptions po;
        Po::ParsedValues  pv = Po::parseCommandString("--flag! --no-flag --no-value", ctx);
        REQUIRE(po.assign(pv) == true);
        REQUIRE((b1 == false && b2 == true && i == 0));

        REQUIRE_THROWS_AS(Po::parseCommandString("--no-value=2", ctx), Po::UnknownOption);
        pv = Po::parseCommandString("--no-value --value=2", ctx);
        REQUIRE_THROWS_AS(Po::ParsedOptions().assign(pv), Po::ValueError);
    }
    SECTION("negatable options should better not be a prefix of other option") {
        b1 = true, b2 = false;
        g.addOptions()("swi!", Po::flag(b1), "A negatable switch")("no-swi2", Po::flag(b2), "A switch");
        ctx.add(g);
        Po::ParsedOptions po;
        Po::ParsedValues  pv = Po::parseCommandString("--no-swi", ctx);
        REQUIRE((po.assign(pv) && b1 && b2));
    }
}

TEST_CASE("Test parsed options", "[options]") {
    Po::OptionGroup g;
    int             i1, i2;
    g.addOptions()("int1", Po::storeTo(i1), "An int")("int2", Po::storeTo(i2)->defaultsTo("10"), "Another int");
    Po::OptionContext ctx;
    ctx.add(g);
    SECTION("assign parsed values") {
        Po::ParsedOptions po;
        Po::ParsedValues  pv = Po::parseCommandString("--int1=2", ctx);
        po.assign(pv);
        ctx.assignDefaults(po);
        REQUIRE(po.contains("int1"));
        REQUIRE_FALSE(po.contains("int2"));
        REQUIRE(i2 == 10); // default value
        REQUIRE(i1 == 2);  // parsed value
        pv.add("int2", "20");
        po.assign(pv);
        REQUIRE(po.contains("int2"));
        REQUIRE(i2 == 20); // parsed value
    }
    SECTION("parsed options support exclude list") {
        Po::ParsedOptions po, po2;
        po.assign(Po::parseCommandString("--int1=1", ctx));
        po2.assign(Po::parseCommandString("--int1=10 --int2=2", ctx), &po);
        REQUIRE((i1 == 1 && i2 == 2));
    }
    SECTION("assign options from multiple sources") {
        Po::OptionGroup g2;
        bool            b1;
        int             int3;
        g2.addOptions()("flag!", Po::flag(b1), "A switch")("int3", Po::storeTo(int3), "Yet another int");
        ctx.add(g2);
        Po::ParsedOptions po;
        po.assign(Po::parseCommandString("--int1=2 --flag --int3=3", ctx));
        REQUIRE((i1 == 2 && b1 == true && int3 == 3));
        Po::ParsedOptions p1(po), p2;
        p1.assign(Po::parseCommandString("--int1=3 --no-flag --int2=4 --int3=5", ctx));
        REQUIRE((i1 == 2 && b1 == true && i2 == 4 && int3 == 3));
        p2.assign(Po::parseCommandString("--int1=3 --no-flag --int2=5 --int3=5", ctx));
        REQUIRE((i1 == 3 && b1 == false && i2 == 5 && int3 == 5));
    }
}

TEST_CASE("Test option groups", "[options]") {
    int             i1, i2;
    Po::OptionGroup g1("Group1");
    g1.addOptions()("int1", Po::storeTo(i1)->defaultsTo("10"), "An int");
    Po::OptionGroup g2("Group2");
    g2.addOptions()("int2", Po::storeTo(i2)->defaultsTo("10"), "An int");
    Po::OptionContext ctx;
    ctx.add(g1);
    ctx.add(g2);
    REQUIRE_THROWS_AS(ctx.findGroup("Foo"), Po::ContextError);
    const Po::OptionGroup& x1 = ctx.findGroup(g1.caption());
    REQUIRE(x1.size() == g1.size());
    for (auto gIt = g1.begin(), xIt = x1.begin(); gIt != g1.end(); ++gIt, ++xIt) {
        REQUIRE(((*gIt)->name() == (*xIt)->name() && (*gIt)->value() == (*xIt)->value()));
    }
}

TEST_CASE("Test context", "[options]") {
    Po::OptionGroup   g;
    Po::OptionContext ctx;
    SECTION("option context supports find") {
        bool b1;
        bool b2;
        g.addOptions()("help", Po::flag(b1), "")("help2", Po::flag(b2), "");
        ctx.add(g);
        REQUIRE(ctx.tryFind("help") != ctx.end());
        REQUIRE(ctx.tryFind("help", Po::OptionContext::find_name_or_prefix) != ctx.end());
        REQUIRE(ctx.tryFind("help", Po::OptionContext::find_prefix) == ctx.end());

        ctx.addAlias("Hilfe", ctx.find("help"));
        REQUIRE(ctx.tryFind("Hilfe") != ctx.end());
    }

    SECTION("option description supports argument description placeholder '%A'") {
        int x;
        g.addOptions()("number", Po::storeTo(x)->arg("<n>"), "Some int %A in %%");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int <n> in %") != std::string::npos);
    }
    SECTION("option description supports default value placeholder '%D'") {
        int x;
        g.addOptions()("foo", Po::storeTo(x)->defaultsTo("99"), "Some int (Default: %D)");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int (Default: 99)") != std::string::npos);
    }
}

TEST_CASE("Test errors", "[options]") {
    Po::OptionGroup      g;
    Po::OptionInitHelper x = g.addOptions();
    Po::OptionContext    ctx;
    bool                 b;
    SECTION("option name must not be empty") {
        REQUIRE_THROWS_AS(x(nullptr, Po::flag(b), ""), Po::Error);
        REQUIRE_THROWS_AS(x("", Po::flag(b), ""), Po::Error);
    }
    SECTION("alias must be a single character") { REQUIRE_THROWS_AS(x("foo,fo", Po::flag(b), ""), Po::Error); }
    SECTION("multiple occurrences are not allowed") {
        g.addOptions()("help", Po::flag(b), "")("rand", Po::flag(b), "");
        ctx.add(g);
        Po::ParsedValues pv(ctx);
        pv.add("help", "1");
        pv.add("help", "1");
        REQUIRE_THROWS_AS(Po::ParsedOptions().assign(pv), Po::ValueError);
    }
    SECTION("unknown options are not allowed") {
        REQUIRE_THROWS_AS(Po::parseCommandString("--help", ctx), Po::UnknownOption);
    }
    SECTION("options must not be ambiguous") {
        g.addOptions()("help", Po::flag(b), "")("help-a", Po::flag(b), "")("help-b", Po::flag(b), "")("help-c",
                                                                                                      Po::flag(b), "");
        ctx.add(g);
        REQUIRE_THROWS_AS(ctx.find("he", Po::OptionContext::find_prefix), Po::AmbiguousOption);
    }
}

TEST_CASE("Test parse argv array", "[options]") {
    const char*     argv[] = {"-h", "-V3", "--int", "6"};
    Po::OptionGroup g;
    bool            x;
    int             i1, i2;
    g.addOptions()("help,h", Po::flag(x), "")("version,V", Po::storeTo(i1), "An int")("int", Po::storeTo(i2),
                                                                                      "Another int");
    Po::OptionContext ctx;
    ctx.add(g);
    Po::ParsedOptions po;
    Po::ParsedValues  pv = Po::parseCommandArray(argv, sizeof(argv) / sizeof(const char*), ctx);
    po.assign(pv);
    REQUIRE(x);
    REQUIRE(i1 == 3);
    REQUIRE(i2 == 6);
}

TEST_CASE("Test parser", "[options]") {
    int             i1, i2;
    bool            flag1 = true, flag2 = false;
    Po::OptionGroup g;
    g.addOptions()("int1", Po::storeTo(i1), "An int")("int2", Po::storeTo(i2), "Another int")(
        "flag", Po::flag(flag1), "A flag")("foo,f", Po::flag(flag2), "A flag");
    SECTION("parser supports custom context") {
        struct PC : public Po::ParseContext {
            Po::OptionGroup* g;
            PC(Po::OptionGroup& grp) : g(&grp) {}
            Po::SharedOptPtr getOption(const char* name, FindType) override {
                for (const auto& opt : *g) {
                    if (opt->name() == name) {
                        return opt;
                    }
                }
                return Po::SharedOptPtr(nullptr);
            }
            Po::SharedOptPtr getOption(int, const char*) override { return Po::SharedOptPtr(nullptr); }
            void             addValue(const Po::SharedOptPtr& key, const std::string& value) override {
                if (not key->value()->parse(key->name(), value, Po::Value::value_unassigned)) {
                    throw std::logic_error("Invalid value");
                }
            }
        } pc(g);
        Po::parseCommandString("--int1=10 --int2 22", pc);
        REQUIRE((i1 == 10 && i2 == 22));
    }
    SECTION("parser optionally supports flags with explicit value") {
        Po::OptionContext ctx;
        ctx.add(g);
        std::string cmd = "--flag=false --foo=on";
        REQUIRE_THROWS_AS(Po::parseCommandString(cmd, ctx, false, nullptr, 0), Po::SyntaxError);
        Po::ParsedOptions().assign(Po::parseCommandString(cmd, ctx, false, nullptr, Po::command_line_allow_flag_value));
        REQUIRE(flag1 == false);
        REQUIRE(flag2 == true);
    }
    SECTION("parser supports quoting") {
        std::vector<std::string> tok;
        g.addOptions()("path", Po::storeTo(tok)->composing(), "An int");
        Po::OptionContext ctx;
        ctx.add(g);
        struct P {
            static bool func(const std::string&, std::string& o) {
                o = "path";
                return true;
            }
        };
        std::string cmd;
        cmd.append("foo bar");
        cmd.append(" \"foo bar\"");
        cmd.append(" '\\foo bar'");
        cmd.append(" \\");
        cmd.append("\"");
        cmd.append("foo bar");
        cmd.append("\\");
        cmd.append("\"");
        Po::ParsedOptions().assign(Po::parseCommandString(cmd, ctx, false, &P::func));
        REQUIRE(tok.size() == 6);
        REQUIRE(tok[0] == "foo");
        REQUIRE(tok[1] == "bar");
        REQUIRE(tok[2] == "foo bar");
        REQUIRE(tok[3] == "\\foo bar");
        REQUIRE(tok[4] == "\"foo");
        REQUIRE(tok[5] == "bar\"");
        tok.clear();
        cmd = R"(\\"Hallo Welt\\")";
        Po::ParsedOptions().assign(Po::parseCommandString(cmd, ctx, false, &P::func));
        REQUIRE(tok.size() == 1);
        REQUIRE(tok[0] == "\\Hallo Welt\\");
    }
}

TEST_CASE("Test gringo example", "[options]") {
    Po::OptionContext ctx;
    Po::OptionGroup   gringo("Gringo Options");
    struct GringoOpts {
        enum class Debug { NONE, TEXT, TRANSLATE, ALL };
        std::string              outputFormat;
        std::vector<std::string> defines;
        Debug                    outputDebug = Debug::NONE;
    };
    GringoOpts opts;
    gringo.addOptions()("text,t", parse([&](const std::string&) {
                                      opts.outputFormat = "text";
                                      return true;
                                  })->flag(),
                        "Print plain text format")("const,c",
                                                   parse([&](const std::string& v) {
                                                       opts.defines.push_back(v);
                                                       return true;
                                                   })
                                                       ->composing()
                                                       ->arg("<id>=<term>"),
                                                   "Replace term occurrences of <id> with <term>")(
        "output-debug",
        storeTo(opts.outputDebug = GringoOpts::Debug::NONE,
                values<GringoOpts::Debug>({{"none", GringoOpts::Debug::NONE},
                                           {"text", GringoOpts::Debug::TEXT},
                                           {"translate", GringoOpts::Debug::TRANSLATE},
                                           {"all", GringoOpts::Debug::ALL}})),
        "Print debug information during output:\n"
        "      none     : no additional info\n"
        "      text     : print rules as plain text (prefix %%)\n"
        "      translate: print translated rules as plain text (prefix %%%%)\n"
        "      all      : combines text and translate");
    ctx.add(gringo);
    REQUIRE_NOTHROW(
        Po::ParsedOptions().assign(Po::parseCommandString("--text -c a=b -c x=y --output-debug=translate", ctx)));

    CHECK(opts.outputFormat == "text");
    CHECK(opts.outputDebug == GringoOpts::Debug::TRANSLATE);
    CHECK(opts.defines == std::vector<std::string>({"a=b", "x=y"}));
}

} // namespace Potassco::ProgramOptions::Test
