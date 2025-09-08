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
#include <potassco/basic_types.h>
#include <potassco/error.h>
#include <potassco/program_opts/typed_value.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <csignal>
#include <sstream>

namespace Potassco::ProgramOptions::Test {
namespace Po = ProgramOptions;

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
        g.addOptions()("-@@1,foo", Po::storeTo(foo), "Option on level 1");
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

TEST_CASE("Test application formatting", "[app]") {
    MyApp app;
    using namespace std::literals;
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
            REQUIRE(s.str() == "\033[1;31m*** ERROR: (TestApp): \033[0m\033[1mAn error\033[0m\n"
                               "\033[1;93m*** Warn : (TestApp): \033[0m\033[1mA warning\033[0m\n");
        }
    }
    SECTION("sink") {
        std::string s;
        app.enableColoredMessages();
        app.writeMessage(s, Application::message_error, "An error");
        CHECK(s == "\033[1;31m*** ERROR: (TestApp): \033[0m\033[1mAn error\033[0m"sv);
        DynamicBuffer db;
        app.writeMessage(db, Application::message_info, "Some info");
        CHECK(db.view() == "\033[1;36m*** Info : (TestApp): \033[0m\033[1mSome info\033[0m"sv);

        struct SpanAdapter {
            std::span<char> buf;
            void            append(std::string_view s) {
                auto n = std::min(buf.size(), s.size());
                std::copy_n(std::data(s), n, buf.data());
                buf = buf.subspan(n);
            }
        } span;
        db.clear();
        span.buf = db.alloc(30);
        app.writeMessage(span, Application::message_info, "Some info");
        CHECK(db.view() == "\033[1;36m*** Info : (TestApp): \033"sv);
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
        auto emph = [](std::string_view m, bool yes) {
            return std::string(yes ? "\033[1m" : "").append(m).append(yes ? "\033[0m" : "");
        };
        auto warn = [](std::string_view m, bool yes) {
            return std::string(yes ? "\033[1;93m" : "").append(m).append(yes ? "\033[0m" : "");
        };
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
    MyApp       app;
    const char* args[] = {"-h", "-V3", "--vers", "hallo"};
    SECTION("args") {
        REQUIRE(app.main(args) == EXIT_SUCCESS);
        REQUIRE(app.getVerbose() == 3);
        REQUIRE(app.input.at(0) == "hallo");
        REQUIRE_FALSE(app.messages["help"].empty());
        REQUIRE(app.messages["version"].empty()); // help processed first
        REQUIRE(app.messages["error"].empty());
        std::string_view help(app.messages["help"]);
        CAPTURE(help);
        REQUIRE(help.starts_with("TestApp version 1.0\n"
                                 "usage: TestApp [options] [files]\n"));

        help.remove_prefix(std::min(help.find("Basic Options:"), help.size()));
        REQUIRE(help.starts_with("Basic Options:\n"));

        constexpr auto contains = [](std::string_view where, std::string_view what) {
            return where.find(what) < where.size();
        };
        CAPTURE(help);
        REQUIRE(contains(help, "-V,--verbose[=<n>]   : Set verbosity level to <n>"));
        REQUIRE(contains(help, "--time-limit=<n>"));
        REQUIRE(contains(help, "Default command-line:\n"
                               "TestApp "));
        help.remove_suffix(help.find("usage"));
        REQUIRE_FALSE(contains(help, "file"));
        REQUIRE_FALSE(contains(help, "foo"));
        REQUIRE_FALSE(contains(help, "E1"));
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
            REQUIRE(app.messages["error"] ==
                    "\033[1;31m*** ERROR: (TestApp): \033[0mIn context '\033[1m<TestApp>\033[0m': '\033[1m3\033[0m' "
                    "invalid value for: '\033[1mhelp\033[0m'\n"
                    "\033[1;36m*** Info : (TestApp): \033[0mTry '\033[1m--help\033[0m' for usage information");
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
    struct TimedApp : MyApp {
        TimedApp() : stop(0) {}
        void run() override {
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
    const char* args[] = {"--time-limit=1"}; // NOLINT
    app.main(args);
    REQUIRE(app.stop == 14);
}
} // namespace Potassco::ProgramOptions::Test
