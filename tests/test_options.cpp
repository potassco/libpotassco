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
#include <ranges>

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
        Po::Option o("other-int", "some other integer", Po::storeTo(x)->arg("<n>"));
        REQUIRE(o.value()->defaultsTo() == static_cast<const char*>(nullptr));
    }
    SECTION("options can have default values") {
        Po::Option o("some-int", "some integer", Po::storeTo(x)->defaultsTo("123")->arg("<n>"));
        REQUIRE(strcmp(o.value()->defaultsTo(), "123") == 0);
        REQUIRE(o.argName() == "<n>");
        REQUIRE(o.assignDefault());
        REQUIRE(x == 123);
        REQUIRE(o.value()->isDefaulted());
    }
    SECTION("careful with invalid default values") {
        Po::Option o("other-int", "some other integer", Po::storeTo(x)->defaultsTo("123Hallo?")->arg("<n>"));
        REQUIRE(not o.assignDefault());
        REQUIRE_FALSE(o.value()->isDefaulted());
    }

    SECTION("parsing overwrites default value") {
        Po::OptionGroup g;
        g.addOptions()("int", Po::storeTo(x)->defaultsTo("10"), "An int");
        Po::OptionContext ctx;
        ctx.add(g);
        Po::ParsedOptions po;
        ctx.assignDefaults(po);
        REQUIRE(x == 10);
        Po::DefaultParseContext pc(ctx);
        pc.setValue(pc.getOption("int", OptionContext::find_name), "2");
        REQUIRE(x == 2);
    }
}
TEST_CASE("Test option supports runtime string", "[options]") {
    SECTION("option name") {
        std::string tmp("number");
        int         x;
        Po::Option  o(tmp.c_str(), "some num", Po::storeTo(x));
        REQUIRE(o.name() == "number");
        REQUIRE(o.value()->rtName());
        REQUIRE(o.description() == "some num");
        REQUIRE_FALSE(o.value()->rtDesc());

        tmp.assign("reused for other stuff");
        REQUIRE(o.name() == "number");
    }

    SECTION("option description") {
        std::string desc("Some option description coming from elsewhere");
        int         x;
        Po::Option  o("number", desc.c_str(), Po::storeTo(x));
        REQUIRE(o.name() == "number");
        REQUIRE(o.description() == desc);
        REQUIRE(o.value()->rtDesc());
        REQUIRE_FALSE(o.value()->rtName());
        REQUIRE((void*) o.description().data() != (void*) desc.c_str());
        desc.clear();
        REQUIRE(o.description() != desc);
    }
}
static bool negatable_int(std::string_view s, int& out) {
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
        Po::Option o("flag", "some flag", Po::flag(b1));
        REQUIRE(o.value()->isNegatable() == false);
    }
    SECTION("exclamation mark ('!') in init helper makes option negatable") {
        g.addOptions()("!,flag", Po::flag(b1), "some flag");
        REQUIRE((*g.begin())->value()->isNegatable() == true);
        ctx.add(g);
        REQUIRE_THROWS_AS(ctx["flag!"], UnknownOption);
    }
    SECTION("exclamation mark ('!') in name") {
        g.addOptions()("flag!", Po::flag(b1), "some flag");
        REQUIRE((*g.begin())->value()->isNegatable() == false);
        ctx.add(g);
        REQUIRE_NOTHROW(ctx["flag!"]);
    }
    SECTION("negatable options are shown in description") {
        int i;
        g.addOptions()                                        //
            ("!-f,flag", Po::flag(b1), "some negatable flag") //
            ("!,value", Po::storeTo(i, &negatable_int)->arg("<n>"), "some negatable int");
        ctx.add(g);
        std::string       help;
        Po::OptionPrinter out(help);
        ctx.description(out);
        CAPTURE(help);
        REQUIRE(help.find("[no-]flag") != std::string::npos);
        REQUIRE(help.find("<n>|no") != std::string::npos);
    }
    SECTION("negatable options are correctly parsed") {
        int i = 123;
        g.addOptions()                                        //
            ("-f!,flag", Po::flag(b1), "some negatable flag") //
            ("flag!", Po::flag(b2), "some flag")              //
            ("!,value", Po::storeTo(i, &negatable_int)->arg("<n>"), "some negatable int");
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        Po::parseCommandString(po, "--flag! --no-flag --no-value");
        REQUIRE((b1 == false && b2 == true && i == 0));

        REQUIRE_THROWS_AS(Po::parseCommandString(po.clearParsed(), "--no-value=2"), Po::UnknownOption);
        REQUIRE_THROWS_AS(Po::parseCommandString(po.clearParsed(), "--no-value --value=2"), Po::ValueError);
    }
    SECTION("negatable options should better not be a prefix of other option") {
        b1 = true, b2 = false;
        g.addOptions()                                               //
            ("swi", Po::flag(b1)->negatable(), "A negatable switch") //
            ("no-swi2", Po::flag(b2), "A switch");
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        Po::parseCommandString(po, "--no-swi");
        REQUIRE((b1 && b2));
    }
}

TEST_CASE("Test parsed options", "[options]") {
    Po::OptionGroup g;
    int             i1, i2;
    g.addOptions()("int1", Po::storeTo(i1), "An int")("int2", Po::storeTo(i2)->defaultsTo("10"), "Another int");
    Po::OptionContext       ctx;
    Po::DefaultParseContext po{ctx};
    ctx.add(g);
    SECTION("assign parsed values") {
        Po::parseCommandString(po, "--int1=2");
        ctx.assignDefaults(po.parsed());
        REQUIRE(po.parsed().contains("int1"));
        REQUIRE_FALSE(po.parsed().contains("int2"));
        REQUIRE(i2 == 10); // default value
        REQUIRE(i1 == 2);  // parsed value
        po.setValue(po.getOption("int2", OptionContext::find_name_or_prefix), "20");
        po.finish(nullptr);
        REQUIRE(po.parsed().contains("int2"));
        REQUIRE(i2 == 20); // parsed value
    }
    SECTION("parse options from multiple sources") {
        Po::OptionGroup g2;
        bool            b1;
        int             int3;
        g2.addOptions()("!,flag", Po::flag(b1), "A switch")("int3", Po::storeTo(int3), "Yet another int");
        ctx.add(g2);
        REQUIRE_NOTHROW(Po::parseCommandString(po, "--int1=2 --flag --int3=3"));
        REQUIRE((i1 == 2 && b1 == true && int3 == 3));

        REQUIRE_NOTHROW(Po::parseCommandString(po, "--int1=3 --no-flag --int2=4 --int3=5"));
        REQUIRE((i1 == 2 && b1 == true && i2 == 4 && int3 == 3));

        REQUIRE_NOTHROW(Po::parseCommandString(po.clearParsed(), "--int1=3 --no-flag --int2=5 --int3=5"));
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
    REQUIRE_THROWS_AS(ctx.group("Foo"), Po::ContextError);
    const auto& x1 = ctx.group(g1.caption());
    REQUIRE(x1.size() == g1.size());
    for (auto gIt = g1.begin(), xIt = x1.begin(); gIt != g1.end(); ++gIt, ++xIt) {
        REQUIRE(((*gIt)->name() == (*xIt)->name() && (*gIt)->value() == (*xIt)->value()));
    }
}

TEST_CASE("Test context", "[options]") {
    Po::OptionGroup   g;
    Po::OptionContext ctx;
    SECTION("option context supports get") {
        bool b1;
        bool b2;
        g.addOptions()("help", Po::flag(b1), "")("help2", Po::flag(b2), "");
        ctx.add(g);
        REQUIRE_NOTHROW(ctx.option("help").get());
        REQUIRE_NOTHROW(ctx.option("help", Po::OptionContext::find_name_or_prefix).get());
        REQUIRE_THROWS_AS(ctx.option("help", Po::OptionContext::find_prefix), AmbiguousOption);

        ctx.addAlias(ctx.optionIndex("help"), "Hilfe");
        REQUIRE_NOTHROW(ctx.option("Hilfe").get());
    }

    SECTION("option description supports argument description placeholder '%A'") {
        int x;
        g.addOptions()("number", Po::storeTo(x)->arg("<n>"), "Some int %A in %%");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int <n> in %") != std::string::npos);
        REQUIRE(ex.find("%%") == std::string::npos);
    }
    SECTION("option description supports default value placeholder '%D'") {
        int x;
        g.addOptions()("foo", Po::storeTo(x)->defaultsTo("99"), "Some int (Default: %D)");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int (Default: 99)") != std::string::npos);
    }
    SECTION("option description supports default value placeholder '%I'") {
        int x;
        g.addOptions()("foo", Po::storeTo(x)->implicit("99"), "Some int (Implicit: %I)");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int (Implicit: 99)") != std::string::npos);
    }
    SECTION("default value format") {
        int                    x;
        std::unique_ptr<Value> val(Po::storeTo(x));
        std::string            out;
        SECTION("empty") {
            DefaultFormat::format(out, "", *val, "");
            REQUIRE(out == "\n");
        }
        SECTION("empty with sep") {
            DefaultFormat::format(out, "", *val);
            REQUIRE(out == ": \n");
        }
        SECTION("missing replace value") {
            DefaultFormat::format(out, "Default: [%D]", *val, "");
            REQUIRE(out == "Default: []\n");
        }
        SECTION("missing replace char") {
            DefaultFormat::format(out, "Default: %", *val, "");
            REQUIRE(out == "Default: \n");
        }
        SECTION("missing unknown replace char") {
            DefaultFormat::format(out, "Default: [%x]", *val, "");
            REQUIRE(out == "Default: [x]\n");
        }
    }
}

TEST_CASE("Test apply spec", "[options]") {
    struct DummyValue : Value {
        bool doParse(std::string_view, std::string_view) override { return true; }
    };
    SECTION("alias") {
        DummyValue v;
        REQUIRE(OptionInitHelper::applySpec("-f", v));
        REQUIRE(v.alias() == 'f');
        SECTION("fail if not single char") { REQUIRE_FALSE(OptionInitHelper::applySpec("-fo", v)); }
        SECTION("fail if missing char") { REQUIRE_FALSE(OptionInitHelper::applySpec("-", v)); }
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionInitHelper::applySpec("-f-q", v)); }
    }
    SECTION("level") {
        DummyValue v;
        REQUIRE(OptionInitHelper::applySpec("@2", v));
        REQUIRE(v.level() == Potassco::ProgramOptions::desc_level_e2);
        SECTION("fail if not a number") { REQUIRE_FALSE(OptionInitHelper::applySpec("@x", v)); }
        SECTION("fail if missing char") { REQUIRE_FALSE(OptionInitHelper::applySpec("@", v)); }
        SECTION("fail if out of bounds") { REQUIRE_FALSE(OptionInitHelper::applySpec("@6", v)); }
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionInitHelper::applySpec("@2@1", v)); }
    }
    SECTION("negatable") {
        DummyValue v;
        REQUIRE_FALSE(v.isFlag());
        REQUIRE(OptionInitHelper::applySpec("*", v));
        REQUIRE(v.isFlag());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionInitHelper::applySpec("**", v)); }
    }
    SECTION("negatable") {
        DummyValue v;
        REQUIRE(OptionInitHelper::applySpec("!", v));
        REQUIRE(v.negatable());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionInitHelper::applySpec("!!", v)); }
    }
    SECTION("composable") {
        DummyValue v;
        REQUIRE(OptionInitHelper::applySpec("+", v));
        REQUIRE(v.composing());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionInitHelper::applySpec("++", v)); }
    }
    SECTION("everything") {
        std::string elems[] = {"*", "!", "+", "@3", "-q"};
        std::ranges::sort(elems);
        while (std::ranges::next_permutation(elems).found) {
            std::string spec;
            DummyValue  v;
            for (const auto& x : elems) { spec += x; }
            CAPTURE(spec);
            REQUIRE(OptionInitHelper::applySpec(spec, v));
            REQUIRE(v.alias() == 'q');
            REQUIRE(v.isFlag());
            REQUIRE(v.negatable());
            REQUIRE(v.composing());
            REQUIRE(v.level() == Potassco::ProgramOptions::desc_level_e3);

            spec.append(elems[1]);
            CAPTURE(spec);
            REQUIRE_FALSE(OptionInitHelper::applySpec(spec, v));
        }
    }
}

TEST_CASE("Test errors", "[options]") {
    Po::OptionGroup      g;
    Po::OptionInitHelper x = g.addOptions();
    Po::OptionContext    ctx;
    bool                 b;
    SECTION("option name must not be empty") { REQUIRE_THROWS_AS(x("", Po::flag(b), ""), Po::Error); }
    SECTION("alias must be a single character") { REQUIRE_THROWS_AS(x("foo", "-fo", Po::flag(b), ""), Po::Error); }
    SECTION("level must be a number") { REQUIRE_THROWS_AS(x("foo", "@x", Po::flag(b), ""), Po::Error); }
    SECTION("level must be in range") { REQUIRE_THROWS_AS(x("foo", "@8", Po::flag(b), ""), Po::Error); }
    SECTION("multiple occurrences are not allowed") {
        g.addOptions()("help", Po::flag(b), "")("rand", Po::flag(b), "");
        ctx.add(g);
        Po::DefaultParseContext pc{ctx};
        pc.setValue(pc.getOption("help", OptionContext::find_name), "1");
        REQUIRE_THROWS_AS(pc.setValue(pc.getOption("help", OptionContext::find_name), "1"), Po::ValueError);
    }
    SECTION("unknown options are not allowed") {
        Po::DefaultParseContext po{ctx};
        REQUIRE_THROWS_AS(Po::parseCommandString(po, "--help"), Po::UnknownOption);
    }
    SECTION("options must not be ambiguous") {
        g.addOptions()("help", Po::flag(b), "")("help-a", Po::flag(b), "")("help-b", Po::flag(b), "")("help-c",
                                                                                                      Po::flag(b), "");
        ctx.add(g);
        REQUIRE_THROWS_AS(ctx.option("he", Po::OptionContext::find_prefix), Po::AmbiguousOption);
    }
}

TEST_CASE("Test parse argv array", "[options]") {
    const char*     argv[] = {"-h", "-V3", "--int", "6"};
    Po::OptionGroup g;
    bool            x;
    int             i1, i2;
    g.addOptions()("-h,help", Po::flag(x), "")    //
        ("-V,version", Po::storeTo(i1), "An int") //
        ("int", Po::storeTo(i2), "Another int");
    Po::OptionContext ctx;
    ctx.add(g);
    Po::DefaultParseContext po{ctx};
    REQUIRE_NOTHROW(Po::parseCommandArray(po, argv));
    REQUIRE(x);
    REQUIRE(i1 == 3);
    REQUIRE(i2 == 6);
}

TEST_CASE("Test parser", "[options]") {
    int             i1, i2;
    bool            flag1 = true, flag2 = false;
    Po::OptionGroup g;
    g.addOptions()                               //
        ("-i,int1", Po::storeTo(i1), "An int")   //
        ("int2", Po::storeTo(i2), "Another int") //
        ("-x,flag", Po::flag(flag1), "A flag")   //
        ("-f,foo", Po::flag(flag2), "A flag");

    SECTION("parser supports custom context") {
        struct PC : public Po::ParseContext {
            Po::OptionGroup* g;
            PC(Po::OptionGroup& grp) : Po::ParseContext("dummy"), g(&grp) {}
            [[nodiscard]] auto state(const Option&) const -> OptState override { return OptState::state_open; }

            Po::SharedOptPtr doGetOption(std::string_view name, FindType) override {
                for (const auto& opt : *g) {
                    if (opt->name() == name) {
                        return opt;
                    }
                }
                return Po::SharedOptPtr(nullptr);
            }
            bool doSetValue(const Po::SharedOptPtr& key, std::string_view value) override {
                return key->value()->parse(key->name(), value);
            }
            void doFinish(const std::exception_ptr&) override {}
        } pc(g);
        REQUIRE_NOTHROW(Po::parseCommandString(pc, "--int1=10 --int2 22"));
        REQUIRE((i1 == 10 && i2 == 22));
    }
    SECTION("parser optionally supports flags with explicit value") {
        Po::OptionContext ctx;
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        std::string             cmd = "--flag=false --foo=on";
        REQUIRE_THROWS_AS(Po::parseCommandString(po, cmd, nullptr, 0), Po::SyntaxError);
        REQUIRE_NOTHROW(Po::parseCommandString(po, cmd, nullptr, Po::command_line_allow_flag_value));
        REQUIRE(flag1 == false);
        REQUIRE(flag2 == true);
    }
    SECTION("parser reports missing value") {
        Po::OptionContext ctx;
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        REQUIRE_THROWS_AS(Po::parseCommandString(po, "--int1"), Po::SyntaxError);
    }
    SECTION("parser supports quoting") {
        std::vector<std::string> tok;
        g.addOptions()("+,path", Po::storeTo(tok), "An int");
        Po::OptionContext ctx;
        ctx.add(g);
        auto positional = [](std::string_view, std::string& o) {
            o = "path";
            return true;
        };
        Po::DefaultParseContext po{ctx};
        std::string             cmd;
        cmd.append("foo bar");
        cmd.append(" \"foo bar\"");
        cmd.append(" '\\foo bar'");
        cmd.append(" \\");
        cmd.append("\"");
        cmd.append("foo bar");
        cmd.append("\\");
        cmd.append("\"");
        REQUIRE_NOTHROW(Po::parseCommandString(po, cmd, positional));
        REQUIRE(tok.size() == 6);
        REQUIRE(tok[0] == "foo");
        REQUIRE(tok[1] == "bar");
        REQUIRE(tok[2] == "foo bar");
        REQUIRE(tok[3] == "\\foo bar");
        REQUIRE(tok[4] == "\"foo");
        REQUIRE(tok[5] == "bar\"");
        tok.clear();
        cmd = R"(\\"Hallo Welt\\")";
        REQUIRE_NOTHROW(Po::parseCommandString(po.clearParsed(), cmd, positional));
        REQUIRE(tok.size() == 1);
        REQUIRE(tok[0] == "\\Hallo Welt\\");
    }
    SECTION("parser supports short flags followed by short option") {
        Po::OptionContext ctx;
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        REQUIRE_NOTHROW(Po::parseCommandString(po, "-xfi10"));
        REQUIRE(flag1 == true);
        REQUIRE(flag2 == true);
        REQUIRE(i1 == 10);
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
    gringo.addOptions() //
        ("text", "-t", parse([&](std::string_view) {
                           opts.outputFormat = "text";
                           return true;
                       })->flag(),
         "Print plain text format") //
        ("-c+,const", parse([&](std::string_view v) {
                          opts.defines.emplace_back(v);
                          return true;
                      })->arg("<id>=<term>"),
         "Replace term occurrences of <id> with <term>") //
        ("output-debug",
         storeTo(opts.outputDebug = GringoOpts::Debug::NONE,
                 values<GringoOpts::Debug>({{"none", GringoOpts::Debug::NONE},
                                            {"text", GringoOpts::Debug::TEXT},
                                            {"translate", GringoOpts::Debug::TRANSLATE},
                                            {"all", GringoOpts::Debug::ALL}})),
         "Print debug information during output:\n"
         "      none     : no additional info\n"
         "      text     : print rules as plain text (prefix %%)\n"
         "      translate: print translated rules as plain text (prefix %%%%)\n"
         "      all      : combines text and translate"); //
    ctx.add(gringo);
    Po::DefaultParseContext po{ctx};
    REQUIRE_NOTHROW(Po::parseCommandString(po, "--text -c a=b -c x=y --output-debug=translate"));

    CHECK(opts.outputFormat == "text");
    CHECK(opts.outputDebug == GringoOpts::Debug::TRANSLATE);
    CHECK(opts.defines == std::vector<std::string>({"a=b", "x=y"}));
}

} // namespace Potassco::ProgramOptions::Test
