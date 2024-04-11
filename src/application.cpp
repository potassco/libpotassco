//
// Copyright (c) 2004 - present, Benjamin Kaufmann
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
//
// NOTE: ProgramOptions is inspired by Boost.Program_options
//       see: www.boost.org/libs/program_options
//
#include <potassco/application.h>

#include <potassco/error.h>
#include <potassco/program_opts/typed_value.h>

#include <climits>
#include <cstdarg>
#include <cstring>
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
#include <csignal>
#include <cstdio>
#include <cstdlib>
#if not defined(SIGALRM)
#define SIGALRM 14
#endif
#if not defined(_WIN32)
#include <unistd.h> // for _exit
static long fetch_and_inc(volatile long& x) { return __sync_fetch_and_add(&x, 1); }
static long fetch_and_dec(volatile long& x) { return __sync_fetch_and_sub(&x, 1); }
#else
#define WIN32_LEAN_AND_MEAN // exclude APIs such as Cryptography, DDE, RPC, Shell, and Windows Sockets.
#if not defined(NOMINMAX)
#define NOMINMAX // do not let windows.h define macros min and max
#endif
#include <process.h>
#include <windows.h>
static long fetch_and_inc(volatile long& x) { return InterlockedIncrement(&x) - 1; }
static long fetch_and_dec(volatile long& x) { return InterlockedDecrement(&x) + 1; }
#endif
using namespace Potassco::ProgramOptions;
using namespace std;
namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// Application
/////////////////////////////////////////////////////////////////////////////////////////
Application* Application::instance_s = nullptr;
Application::Application()
    : exitCode_(EXIT_FAILURE)
    , timeout_(0)
    , verbose_(0)
    , fastExit_(false)
    , blocked_(0)
    , pending_(0) {
    out_ = [](std::string_view v) { fwrite(v.data(), 1, v.size(), stdout); };
    err_ = [](std::string_view v) { fwrite(v.data(), 1, v.size(), stderr); };
}
Application::~Application() { resetInstance(*this); }
void Application::initInstance(Application& app) { instance_s = &app; }
void Application::resetInstance(Application& app) {
    if (instance_s == &app) {
        instance_s = nullptr;
    }
}
#if not defined(_WIN32)
int Application::setAlarm(unsigned sec) {
    if (sec) {
        signal(SIGALRM, &Application::sigHandler);
    }
    alarm(sec);
    return 1;
}
#else
int Application::setAlarm(unsigned sec) {
    static HANDLE alarmEvent  = CreateEvent(0, TRUE, TRUE, TEXT("Potassco::Application::AlarmEvent"));
    static HANDLE alarmThread = INVALID_HANDLE_VALUE;
    if (alarmEvent == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (alarmThread != INVALID_HANDLE_VALUE) {
        // wakeup any existing alarm
        SetEvent(alarmEvent);
        WaitForSingleObject(alarmThread, INFINITE);
        CloseHandle(alarmThread);
        alarmThread = INVALID_HANDLE_VALUE;
    }
    if (sec > 0) {
        struct THUNK {
            static unsigned __stdcall run(void* p) {
                unsigned ms = static_cast<unsigned>(reinterpret_cast<std::size_t>(p));
                if (WaitForSingleObject(alarmEvent, ms) == WAIT_TIMEOUT) {
                    Application::getInstance()->processSignal(SIGALRM);
                }
                return 0;
            }
        };
        ResetEvent(alarmEvent);
        alarmThread = (HANDLE) _beginthreadex(0, 0, &THUNK::run,
                                              reinterpret_cast<void*>(static_cast<std::size_t>(sec) * 1000), 0, 0);
    }
    return 1;
}
#endif

void Application::setOutputSink(Potassco::Application::OutputSink out) { out_ = std::move(out); }
void Application::setErrorSink(Potassco::Application::OutputSink err) { err_ = std::move(err); }

// Application entry point.
int Application::main(int argc, char** argv) {
    initInstance(*this); // singleton instance used for signal handling
    exitCode_ = EXIT_FAILURE;
    blocked_  = 0;
    pending_  = 0;
    if (applyOptions(argc, argv)) {
        // install signal handlers
        for (const int* sig = getSignals(); sig && *sig; ++sig) {
            if (signal(*sig, &Application::sigHandler) == SIG_IGN) {
                signal(*sig, SIG_IGN);
            }
        }
        if (timeout_) {
            if (setAlarm(timeout_) == 0) {
                warn("Could not set time limit!");
            }
        }
        exitCode_ = EXIT_SUCCESS;
        try {
            setup();
            run();
            shutdown(false);
        }
        catch (...) {
            shutdown(true);
        }
    }
    if (fastExit_) {
        exit(exitCode_);
    }
    fflush(stdout);
    fflush(stderr);
    return exitCode_;
}

Application* Application::getInstance() { return instance_s; }

POTASSCO_ATTRIBUTE_FORMAT(4, 0)
static void formatMessage(const Application::OutputSink& sink, const char* sys, const char* type, const char* msg,
                          va_list* args) try {
    va_list argsCopy;
    va_copy(argsCopy, *args);
    auto len = msg && *msg ? std::vsnprintf(nullptr, 0, msg, argsCopy) : 0;
    va_end(argsCopy);
    if (len < 0)
        return;

    auto fullLen = static_cast<size_t>(len) + 2; // newline + \0
    if (type && sys) {
        auto typeLen  = std::strlen(type);
        auto padLen   = typeLen < 5 ? 5 - typeLen : 0;
        fullLen      += typeLen + padLen + std::strlen(sys) + 10;
    }
    auto offSet = std::size_t(0);

    char                    fix[1024];
    std::unique_ptr<char[]> dyn;
    std::span<char>         buf(fix);
    if (fullLen > buf.size()) {
        dyn = std::make_unique<char[]>(fullLen);
        buf = std::span<char>(dyn.get(), dyn.get() + fullLen);
    }
    if (sys && type) {
        auto r = static_cast<std::size_t>(snprintf(buf.data(), buf.size(), "*** %-5s: (%s): ", type, sys));
        offSet = r < buf.size() ? r : 0;
    }
    std::vsnprintf(buf.data() + offSet, buf.size() - offSet, msg, *args);
    buf[fullLen - 2] = '\n';
    buf[fullLen - 1] = '\0';
    sink(std::string_view{buf.data(), fullLen - 1});
}
catch (...) {
}

#define OUTPUT_MESSAGE(SINK, NAME, TYPE, MSG)                                                                          \
    if ((SINK)) {                                                                                                      \
        va_list args;                                                                                                  \
        va_start(args, MSG);                                                                                           \
        formatMessage((SINK), (NAME), (TYPE), MSG, &args);                                                             \
        va_end(args);                                                                                                  \
    }                                                                                                                  \
    else                                                                                                               \
        static_cast<void>(0)

void Application::error(const char* msg, ...) const noexcept { OUTPUT_MESSAGE(err_, getName(), "ERROR", msg); }
void Application::info(const char* msg, ...) const noexcept { OUTPUT_MESSAGE(err_, getName(), "Info", msg); }
void Application::warn(const char* msg, ...) const noexcept { OUTPUT_MESSAGE(err_, getName(), "Warn", msg); }
void Application::println(const char* msg, ...) const noexcept { OUTPUT_MESSAGE(out_, nullptr, nullptr, msg); }

void Application::onUnhandledException() {
    try {
        throw;
    }
    catch (const RuntimeError& e) {
        error("%s", e.what());
        error("%s", e.details().c_str());
    }
    catch (const std::exception& e) {
        error("%s", e.what());
    }
    catch (...) {
        error("Unknown exception");
    }
    exit(EXIT_FAILURE);
}

void Application::setExitCode(int n) { exitCode_ = n; }

int Application::getExitCode() const { return exitCode_; }

// Called on application shutdown
void Application::shutdown(bool hasError) {
    // ignore signals/alarms during shutdown
    fetch_and_inc(blocked_);
    killAlarm();
    if (hasError) {
        onUnhandledException();
    }
    shutdown();
}

void Application::shutdown() {}

// Force exit without calling destructors.
void Application::exit(int exitCode) const {
    fflush(stdout);
    fflush(stderr);
    _exit(exitCode);
}

// Temporarily disable delivery of signals.
int Application::blockSignals() { return (int) fetch_and_inc(blocked_); }

// Re-enable signal handling and deliver any pending signal.
void Application::unblockSignals(bool deliverPending) {
    if (fetch_and_dec(blocked_) == 1) {
        auto pend = static_cast<int>(pending_);
        pending_  = 0;
        // directly deliver any pending signal to our sig handler
        if (pend && deliverPending) {
            processSignal(pend);
        }
    }
}
void Application::sigHandler(int sig) {
    // On Windows and original Unix, a handler once invoked is set to SIG_DFL.
    // Instead, we temporarily ignore signals and reset our handler once it is done.
    POTASSCO_SCOPE_EXIT({ signal(sig, sigHandler); });
    signal(sig, SIG_IGN);
    Application::getInstance()->processSignal(sig);
}

// Called on timeout or signal.
void Application::processSignal(int sig) {
    if (fetch_and_inc(blocked_) == 0) {
        if (not onSignal(sig)) {
            return;
        } // block further signals
    }
    else if (pending_ == 0) { // signals are currently blocked because output is active
        info("Queueing signal...");
        pending_ = sig;
    }
    fetch_and_dec(blocked_);
}

bool Application::onSignal(int x) {
    info("INTERRUPTED by signal!");
    exit(EXIT_FAILURE | (128 + x));
}

// Kill any pending alarm.
void Application::killAlarm() {
    if (timeout_ > 0) {
        setAlarm(0);
    }
}

// Process command-line options.
bool Application::applyOptions(int argc, char** argv) {
    using namespace ProgramOptions;
    unsigned help    = 0;
    bool     version = false;
    try {
        ParsedOptions parsed; // options found in command-line
        OptionContext allOpts(std::string("<").append(getName()).append(">"));
        HelpOpt       helpO = getHelpOption();
        if (helpO.second == 0) {
            error("Invalid help option!");
            exit(EXIT_FAILURE);
        }
        OptionGroup basic("Basic Options");
        Value*      hv = helpO.second == 1
                             ? storeTo(help)->flag()
                             : storeTo(help,
                                       [maxV = helpO.second](const std::string& v, unsigned& out) {
                                      return Potassco::stringTo(v, out) == std::errc{} && out > 0 && out <= maxV;
                                  })
                              ->arg("<n>")
                              ->implicit("1");
        basic.addOptions()                                                                                 //
            ("help,h", hv, helpO.first)                                                                    //
            ("version,v", flag(version), "Print version information and exit")                             //
            ("verbose,V", storeTo(verbose_ = 0)->implicit("-1")->arg("<n>"), "Set verbosity level to %A")  //
            ("time-limit", storeTo(timeout_ = 0)->arg("<n>"), "Set time limit to %A seconds (0=no limit)") //
            ("fast-exit,@1", flag(fastExit_ = false), "Force fast exit (do not call dtors)");              //
        allOpts.add(basic);
        initOptions(allOpts);
        auto values = parseCommandLine(argc, argv, allOpts, false, [this](const std::string& value, std::string& opt) {
            if (const auto* n = getPositional(value); n) {
                opt = n;
                return true;
            }
            return false;
        });
        parsed.assign(values);
        allOpts.assignDefaults(parsed);
        if (help || version) {
            exitCode_ = EXIT_SUCCESS;
            if (help) {
                auto x = (DescriptionLevel) (help - 1);
                allOpts.setActiveDescLevel(x);
                printHelp(allOpts);
            }
            else {
                printVersion();
            }
            return false;
        }
        validateOptions(allOpts, parsed, values);
    }
    catch (const std::exception& e) {
        error("%s", e.what());
        info("Try '--help' for usage information");
        return false;
    }
    return true;
}

void Application::printHelp(const OptionContext& root) {
    println("%s version %s", getName(), getVersion());
    printUsage();
    if (out_) {
        ProgramOptions::OptionPrinter out(out_);
        root.description(out);
        out_("\n");
    }
    printUsage();
    println("Default command-line:\n"
            "%s %s",
            getName(), root.defaults(strlen(getName()) + 1).c_str());
}
void Application::printVersion() {
    println("%s version %s\n"
            "Address model: %d-bit",
            getName(), getVersion(), (int) (sizeof(void*) * CHAR_BIT));
}

void Application::printUsage() { println("usage: %s %s", getName(), getUsage()); }

unsigned Application::verbose() const { return verbose_; }
void     Application::setVerbose(unsigned v) { verbose_ = v; }

} // namespace Potassco
