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
#include <potassco/application.h>
#include <exception>
#include <potassco/basic_types.h>
#include <potassco/error.h>
#include <potassco/program_opts/typed_value.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

POTASSCO_WARNING_IGNORE_MSVC(4996)
#include <atomic>
#include <chrono>
#include <csignal>
#include <sstream>
#include <thread>

namespace Potassco::ProgramOptions::Test {
namespace Po = ProgramOptions;
namespace {
struct MyApp : Application {
    [[nodiscard]] std::string_view getName() const override { return "TestApp"; }
    [[nodiscard]] std::string_view getVersion() const override { return "1.0"; }
    [[nodiscard]] std::string_view getUsage() const override { return "[options] [files]"; }
    [[nodiscard]] HelpOpt getHelpOption() const override { return {"Print {1=basic|2=extended} help and exit", 2}; }
    [[nodiscard]] std::string_view getPositional(std::string_view) const override { return "file"; }
    void                           run() override { setExitCode(doRun ? doRun() : 0); }
    void                           setup() override {}
    void                           initOptions(OptionContext& root) override {
        OptionGroup g("Basic Options");
        g.addOptions()("-x,foo", Po::storeTo(foo).defaultsTo("2"), "Some option with default [%D]");
        root.add(std::move(g));
        OptionGroup g2("E1 Options");
        g2.setDescriptionLevel(Po::desc_level_e1);
        g2.addOptions()("-f+,file", Po::storeTo(input), "Input files");
        root.add(std::move(g2));
    }
    void validateOptions(const OptionContext&, const ParsedOptions&) override {}
    void onHelp(const std::string& str, DescriptionLevel) override { messages["help"].append(str); }
    void onVersion(const std::string& str) override { messages["version"].append(str); }
    bool onUnhandledException(const std::exception_ptr&, std::string_view err) noexcept override {
        messages["error"].append(err);
        return false;
    }
    void flush() override {}
    using StringSeq          = std::vector<std::string>;
    using Messages           = std::map<std::string, std::string>;
    int                  foo = {};
    std::function<int()> doRun;
    StringSeq            input;
    Messages             messages;
};

std::string style(std::string_view msg, const TextStyle& ts) {
    return std::string(ts.view()).append(msg).append(ts.resetView());
}
} // namespace

TEST_CASE("Test application formatting", "[app]") {
    MyApp app;
    using namespace std::literals;
    SECTION("text style") {
        using Color = TextStyle::Color;
        using Emph  = TextStyle::Emphasis;
        STATIC_CHECK(TextStyle().view().empty());
        STATIC_CHECK(TextStyle().resetView().empty());
        STATIC_CHECK(TextStyle(Color::black).view() == "\x1B[0;30m"sv);
        STATIC_CHECK(TextStyle(Color::black | Emph::bold).view() == "\x1B[1;30m"sv);

        STATIC_CHECK(TextStyle(Color::green).view() == "\x1B[0;32m"sv);
        STATIC_CHECK(TextStyle(Color::black | Emph::underline).view() == "\x1B[4;30m"sv);

        STATIC_CHECK(TextStyle(Color::bright_black | Emph::none).view() == "\x1B[0;90m"sv);
        STATIC_CHECK(TextStyle(Color::bright_cyan | Emph::italic).view() == "\x1B[3;96m"sv);
        CHECK(TextStyle(TextStyle::bg(Color::bright_cyan) | Emph::italic).view() == "\x1B[3;106m"sv);

        CHECK(TextStyle(Color::bright_cyan | Emph::italic | TextStyle::bg(Color::black)).view() == "\x1B[3;96;40m"sv);
        CHECK(TextStyle(Color::bright_cyan | Emph::italic | TextStyle::bg(Color::bright_white)).view() ==
              "\x1B[3;96;107m"sv);
        CHECK(TextStyle(Color::bright_cyan | Emph::italic | TextStyle::bg(Color::def)).view() == "\x1B[3;96;49m"sv);

        STATIC_CHECK(MyApp::col_em.view() == "\x1B[1m"sv);
        STATIC_CHECK(MyApp::col_warning.view() == "\x1B[1;93m"sv);
        STATIC_CHECK(MyApp::col_error.view() == "\x1B[1;31m"sv);

        CHECK(TextStyle::fromString("01;31").view() == "\x1B[1;31m"sv);
        CHECK(TextStyle::fromString("32").view() == "\x1B[0;32m"sv);
        CHECK(TextStyle::fromString("4;96").view() == "\x1B[4;96m"sv);
        CHECK(TextStyle::fromString("warning=1;35", 8).view() == "\x1B[1;35m"sv);
        CHECK(TextStyle::fromString("1;39").view() == "\x1B[1;39m"sv);
        CHECK(TextStyle::fromString("").view().empty());
        CHECK(TextStyle::fromString("1").view() == "\x1B[1m"sv);
        CHECK(TextStyle::fromString("0").view().empty());
        CHECK(TextStyle::fromString("4;96;46").view() == "\x1B[4;96;46m"sv);
        CHECK(TextStyle::fromString("4;43;95").view() == "\x1B[4;95;43m"sv);
        CHECK(TextStyle::fromString("1;43").view() == "\x1B[1;43m"sv);
        CHECK(TextStyle::fromString("1;34;49").view() == "\x1B[1;34;49m"sv);

        CHECK_THROWS_AS(TextStyle::fromString("7;31").view(), std::domain_error);
        CHECK_THROWS_AS(TextStyle::fromString("50").view(), std::domain_error);
        CHECK_THROWS_AS(TextStyle::fromString("1;").view(), std::invalid_argument);
        CHECK_THROWS_AS(TextStyle::fromString("300").view(), std::out_of_range);
        CHECK_THROWS_AS(TextStyle::fromString("4;96;46;32").view(), std::invalid_argument);
    }
    SECTION("stream") {
        std::stringstream s;
        s << app.error("An error") << "\n" << app.warn("A warning") << "\n" << app.info("Some info") << "\n";
        REQUIRE(s.str() == "*** ERROR: (TestApp): An error\n"
                           "*** Warn : (TestApp): A warning\n"
                           "*** Info : (TestApp): Some info\n");
        SECTION("color") {
            app.enableColoredMessages();
            s.str("");
            s << app.error("An error") << '\n' << app.warn("A warning") << "\n";
            REQUIRE(s.str() == style("*** ERROR: (TestApp): ", Application::col_error)
                                   .append(style("An error", Application::col_em))
                                   .append("\n")
                                   .append(style("*** Warn : (TestApp): ", Application::col_warning))
                                   .append(style("A warning", Application::col_em))
                                   .append("\n"));
        }
    }
    SECTION("buffer") {
        std::string s;
        app.enableColoredMessages();
        s << app.error("An error");
        CHECK(s ==
              style("*** ERROR: (TestApp): ", Application::col_error).append(style("An error", Application::col_em)));
        DynamicBuffer db;
        db << app.info("Some info");
        CHECK(db.view() ==
              style("*** Info : (TestApp): ", Application::col_info).append(style("Some info", Application::col_em)));

        struct SpanAdapter {
            std::span<char> buf;
            SpanAdapter&    append(std::string_view s) {
                auto n = std::min(buf.size(), s.size());
                std::copy_n(std::data(s), n, buf.data());
                buf = buf.subspan(n);
                return *this;
            }
        } span;
        db.clear();
        span.buf = db.alloc(30);
        span << app.info("Some info");
        auto exp = style("*** Info : (TestApp): ", Application::col_info)
                       .append(style("Some info", Application::col_em))
                       .substr(0, 30);
        CHECK(db.view() == exp);
    }
    SECTION("fail and stop") {
        SECTION("noop if not running") {
            SECTION("fail") {
                REQUIRE_NOTHROW(app.fail(79, "Something is not right!", "Info line 1\nInfo line 2"));
                REQUIRE(app.getExitCode() == EXIT_FAILURE);
            }
            SECTION("stop") {
                REQUIRE_NOTHROW(app.stop(79));
                REQUIRE(app.getExitCode() == EXIT_FAILURE);
            }
        }
        SECTION("stop if running") {
            std::pair<int, std::string> expected;
            const char*                 action = "";
            SECTION("fail") {
                action    = "fail";
                app.doRun = [&] {
                    app.fail(79, "Something is not right!", "Info line 1\nInfo line 2");
                    FAIL("should not be reached");
                    return 0;
                };
                expected.first  = 79;
                expected.second = "*** ERROR: (TestApp): Something is not right!\n"
                                  "*** Info : (TestApp): Info line 1\n"
                                  "*** Info : (TestApp): Info line 2";
            }
            SECTION("stop") {
                action    = "stop";
                app.doRun = [&] {
                    app.stop(12);
                    FAIL("should not be reached");
                    return 0;
                };
                expected.first  = 12;
                expected.second = "";
            }
            CAPTURE(action);
            REQUIRE(app.main({}) == expected.first);
            REQUIRE(app.getExitCode() == expected.first);
            REQUIRE(app.messages["error"] == expected.second);
        }
    }
    SECTION("other errors") {
        auto emph = [](std::string_view m, bool yes) { return style(m, yes ? Application::col_em : TextStyle()); };
        auto warn = [](std::string_view m, bool yes) { return style(m, yes ? Application::col_warning : TextStyle()); };
        std::stringstream expected;
        auto*             what    = GENERATE("default", "colored");
        auto              colored = what == std::string_view{"colored"};
        app.enableColoredMessages(colored);
        CAPTURE(what);
        SECTION("potassco") {
            auto e    = POTASSCO_CAPTURE_EXPRESSION(true == false);
            app.doRun = [&]() -> int { Potassco::failThrow(Errc::overflow_error, e, "kaputt"); };
            expected << app.error() << "kaputt: " << std::strerror(static_cast<int>(Errc::overflow_error)) << '\n'
                     << app.info() << e.location.function_name() << ':' << e.location.line() << ": check '"
                     << emph("true == false", colored) << "' failed.";

            REQUIRE(app.main({}) == EXIT_FAILURE);
            REQUIRE(app.messages["error"] == expected.str());
        }
        SECTION("precondition") {
            auto e    = POTASSCO_CAPTURE_EXPRESSION('x' == 'y');
            app.doRun = [&]() -> int { Potassco::failThrow(Errc::precondition_fail, e, "kaputt"); };
            expected << app.error() << e.location.function_name() << ':' << e.location.line() << ": "
                     << warn("Precondition ", colored) << "'" << emph("'x' == 'y'", colored) << "' "
                     << warn("failed.", colored) << '\n'
                     << app.info() << "message: kaputt";

            REQUIRE(app.main({}) == EXIT_FAILURE);
            REQUIRE(app.messages["error"] == expected.str());
        }
        SECTION("other") {
            app.doRun = []() -> int { throw std::runtime_error("line1\nline2"); };
            expected << app.error() << "line1\n" << app.info() << "line2";

            REQUIRE(app.main({}) == EXIT_FAILURE);
            REQUIRE(app.messages["error"] == expected.str());
        }
    }
}
TEST_CASE("Test application", "[app]") {
    MyApp          app;
    const char*    args[]   = {"-h", "-V3", "--vers", "hallo"};
    constexpr auto contains = [](std::string_view where, std::string_view what) {
        return where.find(what) < where.size();
    };
    SECTION("args") {
        auto input = GENERATE("hallo", "-", "stdin");
        CAPTURE(input);
        *(std::end(args) - 1) = input;
        REQUIRE(app.main(args) == EXIT_SUCCESS);
        REQUIRE(app.getVerbose() == 3);
        REQUIRE(app.input.at(0) == input);
        REQUIRE_FALSE(app.messages["help"].empty());
        REQUIRE(app.messages["version"].empty()); // help processed first
        REQUIRE(app.messages["error"].empty());
        auto help = std::string_view(app.messages["help"]);
        CAPTURE(help);
        REQUIRE(help.starts_with("TestApp version 1.0\n"
                                 "usage: TestApp [options] [files]\n"));

        help.remove_prefix(std::min(help.find("Basic Options:"), help.size()));
        REQUIRE(help.starts_with("Basic Options:\n"));

        CAPTURE(help);
        REQUIRE(contains(help, "-V,--verbose[=<n>]   : Set verbosity level to <n>"));
        REQUIRE(contains(help, "--time-limit=<n>"));
        REQUIRE(contains(help, "Some option with default [2]"));
        REQUIRE(contains(help, "Default command-line:\n"
                               "TestApp --foo=2"));
        help.remove_suffix(help.find("usage"));
        REQUIRE_FALSE(contains(help, "file"));
        REQUIRE_FALSE(contains(help, "foo"));
        REQUIRE_FALSE(contains(help, "E1"));
    }
    SECTION("colored-help") {
        app.enableColoredHelp(true);
        args[0] = "-h";
        REQUIRE(app.main(std::span(args).subspan(0, 1)) == EXIT_SUCCESS);
        auto help = std::string_view(app.messages["help"]);
        REQUIRE(contains(help, std::string("  ")
                                   .append(style("-V", Application::col_opt_short))
                                   .append(",")
                                   .append(style("--verbose", Application::col_opt_long))
                                   .append("[=")
                                   .append(style("<n>", Application::col_opt_arg))
                                   .append("]")));
        REQUIRE(contains(help, std::string("  ")
                                   .append(style("-x", Application::col_opt_short))
                                   .append(",")
                                   .append(style("--foo", Application::col_opt_long))
                                   .append(" ")
                                   .append(style("<arg>", Application::col_opt_arg))));
        REQUIRE(contains(help, std::string("  ")
                                   .append(style("-x", Application::col_opt_short))
                                   .append(",")
                                   .append(style("--foo", Application::col_opt_long))
                                   .append(" ")
                                   .append(style("<arg>", Application::col_opt_arg))));
        REQUIRE(contains(help, std::string("")
                                   .append(style("--time-limit", Application::col_opt_long))
                                   .append("=")
                                   .append(style("<n>", Application::col_opt_arg))));
    }
    SECTION("version") {
        args[0] = "--vers";
        REQUIRE(app.main(std::span(args).subspan(0, 1)) == EXIT_SUCCESS);
        REQUIRE(app.messages["version"].starts_with("TestApp version 1.0\nAddress model: "));
    }
    SECTION("arg error") {
        args[0] = "-h3";
        SECTION("default") {
            REQUIRE(app.main(std::span(args).subspan(0, 1)) == EXIT_FAILURE);
            REQUIRE(app.messages["error"] ==
                    "*** ERROR: (TestApp): In context '<TestApp>': '3' invalid value for: 'help'\n"
                    "*** Info : (TestApp): Try '--help' for usage information");
        }
        SECTION("colored") {
            app.enableColoredMessages(true);
            REQUIRE(app.main(std::span(args).subspan(0, 1)) == EXIT_FAILURE);
            REQUIRE(app.messages["error"] == style("*** ERROR: (TestApp): ", Application::col_error)
                                                 .append("In context '")
                                                 .append(style("<TestApp>", Application::col_em))
                                                 .append("': '")
                                                 .append(style("3", Application::col_em))
                                                 .append("' invalid value for: '")
                                                 .append(style("help", Application::col_em))
                                                 .append("'\n")
                                                 .append(style("*** Info : (TestApp): ", Application::col_info))
                                                 .append("Try '")
                                                 .append(style("--help", Application::col_em))
                                                 .append("' for usage information"));
        }
    }
    SECTION("argv overload") {
        SECTION("skips first") {
            char* argv[] = {(char*) "app", (char*) "--version"}; // NOLINT
            REQUIRE(app.main(2, argv) == EXIT_SUCCESS);
            REQUIRE(app.messages["version"].starts_with("TestApp version 1.0\nAddress model: "));
        }
        SECTION("handles empty") {
            char** argv = nullptr;
            REQUIRE(app.main(0, argv) == EXIT_SUCCESS);
        }
    }
}
TEST_CASE("Test alarm", "[app]") {
#if !defined(__EMSCRIPTEN__)
    SECTION("platform") {
        static std::atomic<int> stop;
        stop = 0;
        REQUIRE(Potassco::setAlarm(100, +[](int s) { stop = s; }) == std::errc{});
        for (int i = 0; stop.load() == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            REQUIRE(i != 100);
        }
        REQUIRE(stop == 14);
        REQUIRE_FALSE(Potassco::killAlarm());
        stop = 0;
        REQUIRE(Potassco::setAlarm(100, +[](int s) { stop = s; }) == std::errc{});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (Potassco::killAlarm()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            REQUIRE(stop.load() == 0);
        }
        else {
            REQUIRE(stop.load() == 14);
        }
        REQUIRE_FALSE(Potassco::killAlarm());
    }
    SECTION("App") {
        struct TimedApp : MyApp {
            TimedApp() : stop(0) {}
            void run() override {
                REQUIRE(getTimeLimit() == 5 * 1000);
                setAlarmMs(100);
                REQUIRE(getTimeLimit() == 100);
                while (stop.load() == 0) { stop.wait(0); }
            }
            bool onSignal(int sig) override {
                stop = sig;
                stop.notify_one();
                return true;
            }
            std::atomic<int> stop;
        };
        TimedApp    app;
        const char* args[] = {"--time-limit=5"}; // NOLINT
        auto        start  = std::chrono::steady_clock::now();
        app.main(args);
        REQUIRE(app.stop == 14);
        auto dur = std::chrono::steady_clock::now() - start;
        REQUIRE(dur < std::chrono::seconds(2));
    }
#else
    SECTION("platform") {
        REQUIRE(
            Potassco::setAlarm(100, +[](int) { FAIL("must not be called"); }) == std::errc::operation_not_supported);
    }
    SECTION("App") {
        struct TimedApp : MyApp {
            TimedApp() = default;
            void run() override { FAIL("must not be called"); }
            bool onUnhandledException(const std::exception_ptr&, std::string_view err) noexcept override {
                error = err;
                return false;
            }
            std::string error;
        };
        TimedApp    app;
        const char* args[] = {"--time-limit=5"}; // NOLINT
        REQUIRE(app.main(args) == EXIT_FAILURE);
        REQUIRE(app.error.find("--time-limit") != std::string::npos);
        REQUIRE(app.error.find("not supported") != std::string::npos);
    }
#endif
}
} // namespace Potassco::ProgramOptions::Test
