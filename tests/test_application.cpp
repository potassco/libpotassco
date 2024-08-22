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
#include <potassco/program_opts/typed_value.h>

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <sstream>

namespace Potassco::ProgramOptions::Test {
namespace Po = ProgramOptions;

struct MyApp : public Potassco::Application {
    [[nodiscard]] const char* getName() const override { return "TestApp"; }
    [[nodiscard]] const char* getVersion() const override { return "1.0"; }
    [[nodiscard]] const char* getUsage() const override { return "[options] [files]"; }
    [[nodiscard]] HelpOpt     getHelpOption() const override { return {"Print {1=basic|2=extended} help and exit", 2}; }
    [[nodiscard]] const char* getPositional(const std::string&) const override { return "file"; }
    void                      run() override { setExitCode(0); }
    void                      setup() override {}
    void                      initOptions(ProgramOptions::OptionContext& root) override {
        Po::OptionGroup g("Basic Options");
        g.addOptions()("foo,@,@1", Po::storeTo(foo), "Option on level 1");
        root.add(g);
        Po::OptionGroup g2("E1 Options");
        g2.setDescriptionLevel(Po::desc_level_e1);
        g2.addOptions()("file,f", Po::storeTo(input)->composing(), "Input files");
        root.add(g2);
    }
    void validateOptions(const Po::OptionContext&, const Po::ParsedOptions&, const Po::ParsedValues&) override {}
    void onHelp(const std::string& str, Potassco::ProgramOptions::DescriptionLevel) override {
        messages["help"].append(str);
    }
    void onVersion(const std::string& str) override { messages["version"].append(str); }
    bool onUnhandledException(const char* err) override {
        messages["error"].append(err);
        return false;
    }
    void flush() override {}
    using StringSeq = std::vector<std::string>;
    using Messages  = std::map<std::string, std::string>;
    int       foo   = {};
    StringSeq input;
    Messages  messages;
};

TEST_CASE("Test application formatting", "[app]") {
    MyApp app;
    SECTION("message") {
        char buffer[80];
        app.formatMessage(buffer, Application::message_error, "An error");
        CHECK(std::strcmp(buffer, "*** ERROR: (TestApp): An error") == 0);

        app.formatMessage(buffer, Application::message_warning, "A warning");
        CHECK(std::strcmp(buffer, "*** Warn : (TestApp): A warning") == 0);

        app.formatMessage(buffer, Application::message_info, "Some info");
        CHECK(std::strcmp(buffer, "*** Info : (TestApp): Some info") == 0);
    }
    SECTION("stream") {
        std::stringstream s;
        s << app.error("An error") << "\n" << app.warn("A warning") << "\n" << app.info("Some info") << "\n";
        REQUIRE(s.str() == "*** ERROR: (TestApp): An error\n"
                           "*** Warn : (TestApp): A warning\n"
                           "*** Info : (TestApp): Some info\n");
    }
}
TEST_CASE("Test application", "[app]") {
    MyApp app;
    char* argv[] = {(char*) "app", (char*) "-h", (char*) "-V3", (char*) "--vers", (char*) "hallo", nullptr}; // NOLINT
    SECTION("args") {
        int argc = 5;
        REQUIRE(app.main(argc, argv) == EXIT_SUCCESS);
        REQUIRE(app.getVerbose() == 3);
        REQUIRE(app.input.at(0) == "hallo");
        REQUIRE_FALSE(app.messages["help"].empty());
        REQUIRE(app.messages["version"].empty()); // help processed first
        REQUIRE(app.messages["error"].empty());
        std::string_view help(app.messages["help"]);
        REQUIRE(help.starts_with("TestApp version 1.0\n"
                                 "usage: TestApp [options] [files]\n"));

        help.remove_prefix(std::min(help.find("Basic Options:"), help.size()));
        REQUIRE(help.starts_with("Basic Options:\n"));

        constexpr auto contains = [](std::string_view where, std::string_view what) {
            return where.find(what) < where.size();
        };
        REQUIRE(contains(help, "--verbose[=<n>],-V"));
        REQUIRE(contains(help, "--time-limit=<n>"));
        REQUIRE(contains(help, "Default command-line:\n"
                               "TestApp "));
        help.remove_suffix(help.find("usage"));
        REQUIRE_FALSE(contains(help, "file"));
        REQUIRE_FALSE(contains(help, "foo"));
        REQUIRE_FALSE(contains(help, "E1"));
    }
    SECTION("version") {
        argv[1]  = (char*) "--vers"; // NOLINT
        argv[2]  = nullptr;
        int argc = 2;
        REQUIRE(app.main(argc, argv) == EXIT_SUCCESS);
        REQUIRE(app.messages["version"].starts_with("TestApp version 1.0\nAddress model: "));
    }
    SECTION("arg error") {
        argv[1] = (char*) "-h3"; // NOLINT
        argv[2] = nullptr;
        REQUIRE(app.main(2, argv) == EXIT_FAILURE);
        REQUIRE(app.messages["error"] == "*** ERROR: (TestApp): In context '<TestApp>': '3' invalid value for: 'help'\n"
                                         "*** Info : (TestApp): Try '--help' for usage information");
    }
}
TEST_CASE("Test alarm", "[app]") {
    struct TimedApp : MyApp {
        TimedApp() : stop(0) {}
        void run() override {
            int i = 0;
            while (not stop) { ++i; }
            setExitCode(i);
        }
        bool onSignal(int) override {
            stop = 1;
            return true;
        }
        volatile sig_atomic_t stop;
    };

    TimedApp app;
    char*    argv[] = {(char*) "app", (char*) "--time-limit=1", nullptr}; // NOLINT
    int      argc   = 2;
    app.main(argc, argv);
    REQUIRE(app.stop == 1);
}
} // namespace Potassco::ProgramOptions::Test
