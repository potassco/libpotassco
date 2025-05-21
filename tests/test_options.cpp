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
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <ranges>

namespace Potassco::ProgramOptions::Test {
namespace Po = ProgramOptions;
using namespace std::literals;
namespace {
struct Foo {
    constexpr explicit Foo(int& c) : count(&c) { ++*count; }
    constexpr ~Foo() { --*count; }
    Foo(const Foo&)                   = delete;
    Foo&        operator=(const Foo&) = delete;
    int         x                     = 12;
    int*        count                 = nullptr;
    int         rc{1};
    friend int  intrusiveCount(const Foo* f) { return f->rc; }
    friend void intrusiveAddRef(Foo* f) { ++f->rc; }
    friend int  intrusiveRelease(Foo* f) { return --f->rc; }
};

} // namespace
TEST_CASE("Test intrusive pointer", "[options]") {
    int count = 0;

    auto ptr = Po::makeShared<Foo>(count);
    CHECK(ptr.count() == 1);
    CHECK(ptr.unique());
    CHECK(count == 1);

    // Disable bogus gcc use-after-free warning with -O2 (or above) in following section.
    POTASSCO_WARNING_PUSH()
    POTASSCO_WARNING_IGNORE_GCC("-Wuse-after-free")
    SECTION("copy") {
        auto ptr2 = ptr;
        CHECK(ptr2.count() == 2);
        Po::IntrusiveSharedPtr<Foo> ptr3;
        ptr3 = ptr2;
        CHECK(ptr3.count() == 3);
        CHECK_FALSE(ptr3.unique());
        ptr2 = ptr3;
        CHECK(ptr2.count() == 3);
        ptr2->x = 77;
    }
    POTASSCO_WARNING_POP()

    SECTION("move") {
        auto ptr2 = std::move(ptr);
        CHECK(ptr2.count() == 1);
        CHECK(ptr.get() == nullptr);
        Po::IntrusiveSharedPtr<Foo> ptr3;
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
    CHECK(ptr.unique());
}

TEST_CASE("Test Str", "[options]") {
    SECTION("literal is detected") {
        Str x("Hallo");
        REQUIRE(x.isLit());
        REQUIRE(x.c_str() == "Hallo"s);
    }
    SECTION("non literal is detected") {
        std::string s("Hallo");
        Str         x(s.c_str());
        REQUIRE_FALSE(x.isLit());
        REQUIRE(x.c_str() == s);
    }
    SECTION("remove prefix") {
        std::string s("Hallo");
        Str         lit("Hallo");
        Str         dyn(s.c_str());
        lit.removePrefix(2);
        dyn.removePrefix(4);
        REQUIRE(lit.c_str() == "llo"s);
        REQUIRE(dyn.c_str() == "o"s);
    }
}

TEST_CASE("Test value desc", "[options]") {
    SECTION("from factory") {
        SECTION("storeTo") {
            SECTION("default parser") {
                int  x;
                auto o = Option("foo", "bar", storeTo(x));
                REQUIRE((o.assign("12") && x == 12));
                REQUIRE_FALSE(o.assign("13foo"));
            }
            SECTION("custom parser") {
                bool parsed = false;
                auto o      = Option("foo", "bar",
                                     Po::storeTo(
                                    parsed, +[](std::string_view val, bool& p) {
                                        p = val == "bla"sv;
                                        return true;
                                    }));
                REQUIRE(o.assign("bla"));
                REQUIRE(parsed);
            }
            SECTION("custom parser lvalue") {
                bool parsed = false;
                auto parser = [](std::string_view val, bool& p) {
                    p = val == "bla"sv;
                    return true;
                };
                auto o = Option("foo", "bar", Po::storeTo(parsed, parser));
                REQUIRE(o.assign("bla"));
                REQUIRE(parsed);
            }
        }
        SECTION("action") {
            std::map<std::string, int> m;
            auto                       o1 =
                Option("foo", "", Po::action<int>([&](const Option& opt, int v) { m[std::string(opt.name())] = v; }));
            auto o2     = Option("bar", "", Po::action<int>([&](int v) { m["v2"] = v; }));
            auto lvalue = [&](int v) { m["v3"] = v; };
            auto o3     = Option("bar", "", Po::action<int>(lvalue));

            CHECK(o1.assign("123"));
            CHECK(o2.assign("342"));
            CHECK(o3.assign("456"));
            REQUIRE(m.size() == 3);
            REQUIRE(m["foo"] == 123);
            REQUIRE(m["v2"] == 342);
            REQUIRE(m["v3"] == 456);
        }
        SECTION("custom") {
            std::map<std::string, int> m;
            auto                       store = [&](std::string_view k, std::string_view v) {
                if (int temp; Potassco::stringTo(v, temp) == std::errc{}) {
                    m[std::string(k)] = temp;
                    return true;
                }
                return false;
            };
            auto o1  = Option("foo", "",
                              Po::parse([&](const Option& opt, std::string_view v) { return store(opt.name(), v); }));
            auto p   = [&](std::string_view v) { return store("v2", v); };
            auto o2  = Option("bar", "", Po::parse(p));
            auto o3M = 0;
            auto o3  = Option("blub", "", Po::parse([&, m = 0](std::string_view v) mutable {
                                 o3M = ++m;
                                 return store("v3", v);
                             }));
            REQUIRE(o1.assign("123"));
            REQUIRE(o2.assign("342"));
            REQUIRE(o3.assign("999"));
            REQUIRE_FALSE(o1.assign("x12"));
            REQUIRE(m.size() == 3);
            REQUIRE(m["foo"] == 123);
            REQUIRE(m["v2"] == 342);
            REQUIRE(m["v3"] == 999);
            REQUIRE(o3M == 1);
        }

        SECTION("shared action") {
            std::map<std::string, std::string> m;
            auto                               action = Po::makeCustom([&](const Option& opt, std::string_view v) {
                if (not v.empty()) {
                    m[std::string(opt.name())] = v;
                    return true;
                }
                return false;
            });
            {
                auto o1 = Option("foo", "", value(action));
                auto o2 = Option("bar", "", value(action).implicit("234"));
                REQUIRE(action.count() == 3);
                REQUIRE(o1.assign("123"));
                REQUIRE(o2.assign(""));
                REQUIRE_FALSE(o1.assign(""));
                REQUIRE(m.size() == 2);
                REQUIRE(m["foo"] == "123");
                REQUIRE(m["bar"] == "234");
            }
            REQUIRE(action.unique());
        }

        SECTION("allowed values") {
            enum class Color { red = 2, green = 10, blue = 20 };

            Color c;
            auto  o = Option("foo", "",
                             Po::storeTo(c, Po::values<Color>({
                                               {"Red", Color::red},
                                               {"Green", Color::green},
                                               {"Blue", Color::blue},
                                           })));

            REQUIRE((o.assign("Red") && c == Color::red));
            REQUIRE((o.assign("GREEN") && c == Color::green));
            REQUIRE_FALSE(o.assign("Blu"));
        }
    }
    SECTION("default value") {
        int x;
        SECTION("no default by default") {
            Po::Option o("other-int", "some other integer", Po::storeTo(x));
            REQUIRE(o.defaultValue().empty());
            REQUIRE(o.argName() == "<arg>"sv);
        }
        SECTION("not defaulted by default") {
            Po::Option o("some-int", "some integer", Po::storeTo(x).defaultsTo("123").arg("<n>"));
            REQUIRE(o.defaultValue() == "123");
            REQUIRE_FALSE(o.defaulted());
            REQUIRE(o.argName() == "<n>");
            REQUIRE(o.assignDefault());
            REQUIRE(x == 123);
            REQUIRE(o.defaulted());
        }
        SECTION("can be defaulted on construction") {
            Po::Option o("some-int", "some integer", Po::storeTo(x).defaultsTo("123", true).arg("<n>"));
            REQUIRE(o.defaultValue() == "123");
            REQUIRE(o.defaulted());
            REQUIRE(o.assign("82"));
            REQUIRE_FALSE(o.defaulted());
        }
        SECTION("careful with invalid default values") {
            Po::Option o("other-int", "some other integer", Po::storeTo(x).defaultsTo("123Hallo?").arg("<n>"));
            REQUIRE_FALSE(o.assignDefault());
            REQUIRE_FALSE(o.defaulted());
        }
        SECTION("can be runtime string") {
            std::string d("defaultValue");
            Po::Option  o("foo", "", Po::storeTo(x).defaultsTo(d.c_str()).arg("<n>"));
            d.clear();
            REQUIRE(o.defaultValue() == "defaultValue"s);
        }
    }
    SECTION("implicit value") {
        SECTION("defaults to 1 for flags") {
            bool       f;
            Po::Option o("flag", "", Po::flag(f));
            REQUIRE(o.flag());
            REQUIRE(o.implicit());
            REQUIRE(o.implicitValue() == "1"sv);
        }
        SECTION("flag does not override explicit implicit value") {
            int        x;
            Po::Option o("flag", "", Po::storeTo(x).implicit("2").flag());
            REQUIRE(o.argName().empty());
            REQUIRE(o.implicitValue() == "2"s);
        }
        SECTION("can be runtime string") {
            std::string imp("1234");
            int         x;
            Po::Option  o("foo", "", Po::storeTo(x).implicit(imp.c_str()));
            imp.clear();
            REQUIRE(o.implicitValue() == "1234"s);
            REQUIRE(o.implicit());
        }
        SECTION("is used when value is empty") {
            int        x;
            Po::Option o("x", "", Po::storeTo(x).implicit("102"));
            REQUIRE((o.assign("") && x == 102));
            REQUIRE((o.assign("456") && x == 456));
        }
    }
    SECTION("flag") {
        SECTION("properties") {
            bool   loud;
            Option o("foo", "bar", Po::flag(loud));
            REQUIRE(o.implicit() == true);
            REQUIRE(o.flag() == true);
            REQUIRE(o.implicitValue() == "1"s);
        }
        SECTION("default parser stores true") {
            bool   loud;
            Option o("foo", "bar", Po::flag(loud));
            REQUIRE((o.assign("") && loud == true));
        }
        SECTION("store_false parser stores false") {
            bool   loud;
            Option o("foo", "bar", Po::flag(loud, Po::store_false));
            REQUIRE((o.assign("") && loud == false));
            REQUIRE((o.assign("0") && loud == true));
        }
        SECTION("valid values") {
            bool   loud;
            Option o("foo", "bar", Po::flag(loud));
            auto   v = GENERATE("on"sv, "1"sv, "yes"sv, "true"sv);
            CAPTURE(v);
            loud = false;
            REQUIRE((o.assign(v) && loud == true));
            REQUIRE_FALSE(o.assign(std::string(v) + "x"));
            v = GENERATE("off"sv, "0"sv, "no"sv, "false"sv);
            CAPTURE(v);
            loud = true;
            REQUIRE((o.assign(v) && loud == false));
            REQUIRE_FALSE(o.assign(std::string(v) + "y"));
        }
        SECTION("alternative action for flag") {
            bool   got = true;
            Option q("quiet", "bar", Po::flag([&](bool val) { got = val; }, Po::store_false));
            REQUIRE((q.assign("") && got == false));
            REQUIRE((q.assign("off") && got == true));
        }
        SECTION("flag init") {
            bool b      = true;
            std::ignore = Po::flag(b, false);
            REQUIRE_FALSE(b);
            std::ignore = Po::flag(b, true, Po::store_false);
            REQUIRE(b);
        }
    }
}
TEST_CASE("Test option supports runtime string", "[options]") {
    SECTION("option name") {
        std::string tmp("number");
        int         x;
        Po::Option  o(tmp.c_str(), "some num", Po::storeTo(x));
        REQUIRE(o.name() == "number");
        REQUIRE(o.description() == "some num");

        tmp.assign("reused for other stuff");
        REQUIRE(o.name() == "number");
    }

    SECTION("option description") {
        std::string desc("Some option description coming from elsewhere");
        int         x;
        Po::Option  o("number", desc.c_str(), Po::storeTo(x));
        REQUIRE(o.name() == "number");
        REQUIRE(o.description() == desc);
        REQUIRE((void*) o.description().data() != (void*) desc.c_str());
        desc.clear();
        REQUIRE(o.description() != desc);
    }

    SECTION("option argument") {
        std::string arg("<foo,bar>");
        int         x;
        Po::Option  o("number", "", Po::storeTo(x).arg(arg.c_str()));
        REQUIRE((void*) o.argName().data() != (void*) arg.c_str());
        arg.clear();
        REQUIRE(o.argName() == "<foo,bar>"s);
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
        REQUIRE_FALSE(o.negatable());
    }
    SECTION("exclamation mark ('!') in init helper makes option negatable") {
        g.addOptions()("!,flag", Po::flag(b1), "some flag");
        REQUIRE(g[0]->negatable());
        ctx.add(g);
        REQUIRE_THROWS_AS(ctx["flag!"], UnknownOption);
    }
    SECTION("exclamation mark ('!') in name") {
        g.addOptions()("flag!", Po::flag(b1), "some flag");
        REQUIRE_FALSE(g[0]->negatable());
        ctx.add(g);
        REQUIRE_NOTHROW(ctx["flag!"]);
    }
    SECTION("negatable options are shown in description") {
        int i;
        g.addOptions()                                        //
            ("!-f,flag", Po::flag(b1), "some negatable flag") //
            ("!,value", Po::storeTo(i, &negatable_int).arg("<n>"), "some negatable int");
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
            ("!,value", Po::storeTo(i, &negatable_int).arg("<n>"), "some negatable int");
        ctx.add(g);
        Po::DefaultParseContext po{ctx};
        Po::parseCommandString(po, "--flag! --no-flag --no-value");
        REQUIRE((b1 == false && b2 == true && i == 0));

        REQUIRE_THROWS_AS(Po::parseCommandString(po.clearParsed(), "--no-value=2"), Po::UnknownOption);
        REQUIRE_THROWS_AS(Po::parseCommandString(po.clearParsed(), "--no-value --value=2"), Po::ValueError);
    }
    SECTION("negatable options should better not be a prefix of other option") {
        b1 = true, b2 = false;
        g.addOptions()                                              //
            ("swi", Po::flag(b1).negatable(), "A negatable switch") //
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
    g.addOptions()                          //
        ("int1", Po::storeTo(i1), "An int") //
        ("int2", Po::storeTo(i2).defaultsTo("10"), "Another int");
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
        po.setValue(*po.getOption("int2", OptionContext::find_name_or_prefix), "20");
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
    g1.addOptions()("int1", Po::storeTo(i1).defaultsTo("10"), "An int");
    Po::OptionGroup g2("Group2");
    g2.addOptions()("int2", Po::storeTo(i2).defaultsTo("10"), "An int");
    Po::OptionContext ctx;
    REQUIRE_FALSE(g2.empty());
    REQUIRE_FALSE(g1.empty());
    ctx.add(g1);
    ctx.add(std::move(g2));
    REQUIRE_THROWS_AS(ctx.group("Foo"), Po::ContextError);
    REQUIRE(g2.empty()); // NOLINT
    REQUIRE_NOTHROW(ctx["int2"].defaultValue() == "10");

    const auto& x1 = ctx.group(g1.caption());
    REQUIRE(x1.size() == g1.size());
    REQUIRE(std::ranges::equal(x1.options(), g1.options(),
                               [](const auto& lhs, const auto& rhs) { return lhs->name() == rhs->name(); }));
}

TEST_CASE("Test context", "[options]") {
    SECTION("option context supports get") {
        Po::OptionGroup   g;
        bool              b1;
        bool              b2;
        Po::OptionContext ctx;
        g.addOptions()                 //
            ("help", Po::flag(b1), "") //
            ("help2", Po::flag(b2), "");
        ctx.add(g);
        REQUIRE_NOTHROW(ctx.option("help"));
        REQUIRE_NOTHROW(ctx.option("help", Po::OptionContext::find_name_or_prefix));
        REQUIRE_THROWS_AS(ctx.option("help", Po::OptionContext::find_prefix), AmbiguousOption);

        ctx.addAlias(ctx.index("help"), "Hilfe");
        REQUIRE(&ctx.option("Hilfe") == &ctx.option("help"));
    }
}

TEST_CASE("Test option format", "[options]") {
    OptionGroup g;
    SECTION("description supports argument description placeholder '%A'") {
        int x;
        g.addOptions()("number", Po::storeTo(x).arg("<n>"), "Some int %A in %%");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int <n> in %") != std::string::npos);
        REQUIRE(ex.find("%%") == std::string::npos);
    }
    SECTION("description supports default value placeholder '%D'") {
        int x;
        g.addOptions()("foo", Po::storeTo(x).defaultsTo("99"), "Some int (Default: %D)");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int (Default: 99)") != std::string::npos);
    }
    SECTION("description supports default value placeholder '%I'") {
        int x;
        g.addOptions()("foo", Po::storeTo(x).implicit("99"), "Some int (Implicit: %I)");
        std::string       ex;
        Po::OptionPrinter out(ex);
        g.format(out, 20);
        REQUIRE(ex.find("Some int (Implicit: 99)") != std::string::npos);
    }
    SECTION("description format") {
        int         x;
        std::string out;
        SECTION("empty") {
            auto opt = Option("foo", "", Po::storeTo(x));
            REQUIRE(opt.description(out).empty());
        }
        SECTION("missing replace value") {
            auto opt = Option("foo", "Default: [%D]", Po::storeTo(x));
            REQUIRE(opt.description(out) == "Default: []");
        }
        SECTION("missing replace char") {
            auto opt = Option("foo", "Default: %", Po::storeTo(x));
            REQUIRE(opt.description(out) == "Default: ");
        }
        SECTION("unknown replace char") {
            auto opt = Option("foo", "Default: [%x]", Po::storeTo(x));
            REQUIRE(opt.description(out) == "Default: [x]");
        }
    }
    SECTION("default option format") {
        std::string out;
        SECTION("long name only") {
            DefaultFormat::format(out, Option("number", "a number", {}, 0), 0);
            REQUIRE(out == "  --number=<arg>: a number\n");
        }
        SECTION("long and short name") {
            DefaultFormat::format(out, Option("number", "a number", {}, 'n'), 0);
            REQUIRE(out == "  -n,--number <arg>: a number\n");
        }
        SECTION("flag") {
            DefaultFormat::format(out, Option("number", "a number", ValueDesc{}.flag(), 'n'), 0);
            REQUIRE(out == "  -n,--number: a number\n");
        }
        SECTION("negatable") {
            DefaultFormat::format(out, Option("number", "a number", ValueDesc{}.negatable(), 'n'), 0);
            REQUIRE(out == "  -n,--number <arg>|no: a number\n");
        }
        SECTION("negatable flag") {
            DefaultFormat::format(out, Option("number", "a number", ValueDesc{}.negatable().flag(), 'n'), 0);
            REQUIRE(out == "  -n,--[no-]number: a number\n");
        }
        SECTION("width") {
            DefaultFormat::format(out, Option("number", "a number", {}, 0), 20);
            REQUIRE(out == "  --number=<arg>    : a number\n");
        }
    }
}

TEST_CASE("Test apply spec", "[options]") {
    SECTION("alias") {
        auto v = ValueDesc();
        char a = 0;
        REQUIRE(OptionGroup::Init::applySpec("-f", v, a));
        REQUIRE(a == 'f');
        SECTION("fail if not single char") { REQUIRE_FALSE(OptionGroup::Init::applySpec("-fo", v, a)); }
        SECTION("fail if missing char") { REQUIRE_FALSE(OptionGroup::Init::applySpec("-", v, a)); }
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionGroup::Init::applySpec("-f-q", v, a)); }
    }
    SECTION("level") {
        auto v = ValueDesc();
        char a = 0;
        REQUIRE(OptionGroup::Init::applySpec("@2", v, a));
        REQUIRE(v.level() == Potassco::ProgramOptions::desc_level_e2);
        SECTION("fail if not a number") { REQUIRE_FALSE(OptionGroup::Init::applySpec("@x", v, a)); }
        SECTION("fail if missing char") { REQUIRE_FALSE(OptionGroup::Init::applySpec("@", v, a)); }
        SECTION("fail if out of bounds") { REQUIRE_FALSE(OptionGroup::Init::applySpec("@6", v, a)); }
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionGroup::Init::applySpec("@2@1", v, a)); }
    }
    SECTION("negatable") {
        auto v = ValueDesc();
        char a = 0;
        REQUIRE_FALSE(v.isFlag());
        REQUIRE(OptionGroup::Init::applySpec("*", v, a));
        REQUIRE(v.isFlag());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionGroup::Init::applySpec("**", v, a)); }
    }
    SECTION("negatable") {
        auto v = ValueDesc();
        char a = 0;
        REQUIRE(OptionGroup::Init::applySpec("!", v, a));
        REQUIRE(v.isNegatable());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionGroup::Init::applySpec("!!", v, a)); }
    }
    SECTION("composable") {
        auto v = ValueDesc();
        char a = 0;
        REQUIRE(OptionGroup::Init::applySpec("+", v, a));
        REQUIRE(v.isComposing());
        SECTION("fail if duplicate") { REQUIRE_FALSE(OptionGroup::Init::applySpec("++", v, a)); }
    }
    SECTION("everything") {
        std::string elems[] = {"*", "!", "+", "@3", "-q"};
        std::ranges::sort(elems);
        while (std::ranges::next_permutation(elems).found) {
            std::string spec;
            auto        v = ValueDesc();
            char        a = 0;
            for (const auto& x : elems) { spec += x; }
            CAPTURE(spec);
            REQUIRE(OptionGroup::Init::applySpec(spec, v, a));
            REQUIRE(a == 'q');
            REQUIRE(v.isFlag());
            REQUIRE(v.isNegatable());
            REQUIRE(v.isComposing());
            REQUIRE(v.level() == Potassco::ProgramOptions::desc_level_e3);

            spec.append(elems[1]);
            CAPTURE(spec);
            REQUIRE_FALSE(OptionGroup::Init::applySpec(spec, v, a));
        }
    }
}

TEST_CASE("Test errors", "[options]") {
    Po::OptionGroup   g;
    auto              x = g.addOptions();
    Po::OptionContext ctx;
    bool              b;
    SECTION("option name must not be empty") { REQUIRE_THROWS_AS(x("", Po::flag(b), ""), Po::Error); }
    SECTION("alias must be a single character") { REQUIRE_THROWS_AS(x("foo", "-fo", Po::flag(b), ""), Po::Error); }
    SECTION("level must be a number") { REQUIRE_THROWS_AS(x("foo", "@x", Po::flag(b), ""), Po::Error); }
    SECTION("level must be in range") { REQUIRE_THROWS_AS(x("foo", "@8", Po::flag(b), ""), Po::Error); }
    SECTION("multiple occurrences are not allowed") {
        g.addOptions()("help", Po::flag(b), "")("rand", Po::flag(b), "");
        ctx.add(g);
        Po::DefaultParseContext pc{ctx};
        pc.setValue(*pc.getOption("help", OptionContext::find_name), "1");
        REQUIRE_THROWS_AS(pc.setValue(*pc.getOption("help", OptionContext::find_name), "1"), Po::ValueError);
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
        struct PC : ParseContext {
            OptionGroup* g;
            PC(OptionGroup& grp) : ParseContext("dummy"), g(&grp) {}
            [[nodiscard]] auto state(const Option&) const -> OptState override { return OptState::state_open; }

            Option* doGetOption(std::string_view name, FindType) override { return g->find(name); }
            bool    doSetValue(Option& opt, std::string_view value) override { return opt.assign(value); }
            void    doFinish(const std::exception_ptr&) override {}
        } pc(g);
        REQUIRE_NOTHROW(Po::parseCommandString(pc, "--int1=10 --int2 22"));
        REQUIRE((i1 == 10 && i2 == 22));
    }
    SECTION("parser optionally supports flags with explicit value") {
        OptionContext ctx;
        ctx.add(g);
        DefaultParseContext po{ctx};
        std::string         cmd = "--flag=false --foo=on";
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
        enum class Debug { NONE, TEXT, TRANSLATE, ALL }; // NOLINT
        std::string              outputFormat;
        std::vector<std::string> defines;
        Debug                    outputDebug = Debug::NONE;
    };
    GringoOpts opts;
    gringo.addOptions() //
        ("text", "-t", parse([&](std::string_view) {
                           opts.outputFormat = "text";
                           return true;
                       }).flag(),
         "Print plain text format") //
        ("-c+,const", parse([&](std::string_view v) {
                          opts.defines.emplace_back(v);
                          return true;
                      }).arg("<id>=<term>"),
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
