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
#include <cstring>
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <syncstream>
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
    : out_(std::cout)
    , err_(std::cerr)
    , exitCode_(EXIT_FAILURE)
    , timeout_(0)
    , verbose_(0)
    , fastExit_(false)
    , blocked_(0)
    , pending_(0) {}
Application::~Application() {
    flush();
    resetInstance(*this);
}
void Application::initInstance(Application& app) { instance_s = &app; }
void Application::resetInstance(Application& app) {
    if (instance_s == &app) {
        instance_s = nullptr;
    }
}
#if not defined(_WIN32)
void Application::setAlarm(unsigned sec) {
    if (sec) {
        signal(SIGALRM, &Application::sigHandler);
    }
    alarm(sec);
}
#else
void Application::setAlarm(unsigned sec) {
    static HANDLE alarmEvent  = CreateEvent(0, TRUE, TRUE, TEXT("Potassco::Application::AlarmEvent"));
    static HANDLE alarmThread = INVALID_HANDLE_VALUE;
    if (alarmEvent == INVALID_HANDLE_VALUE) {
        warn() << "Could not set time limit!\n";
        return;
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
}
#endif

std::ostream& Application::errorPrefix(std::ostream& os) const { return os << "*** ERROR: (" << getName() << "): "; }
std::ostream& Application::infoPrefix(std::ostream& os) const { return os << "*** Info : (" << getName() << "): "; }
std::ostream& Application::warnPrefix(std::ostream& os) const { return os << "*** Warn : (" << getName() << "): "; }

void Application::setStdout(std::ostream& os) { out_ = std::ref(os); }
void Application::setStderr(std::ostream& err) { err_ = std::ref(err); }
void Application::flush() const {
    std::osyncstream(out_).emit();
    std::osyncstream(err_).emit();
    fflush(stderr);
    fflush(stdout);
}

std::osyncstream Application::error() const {
    std::osyncstream out(err_);
    errorPrefix(out);
    return out;
}

std::osyncstream Application::warn() const {
    std::osyncstream out(err_);
    warnPrefix(out);
    return out;
}

std::osyncstream Application::info() const {
    std::osyncstream out(err_);
    infoPrefix(out);
    return out;
}

std::osyncstream Application::log() const { return std::osyncstream(out_); }

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
            setAlarm(timeout_);
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
    flush();
    return exitCode_;
}

Application* Application::getInstance() { return instance_s; }

void Application::onUnhandledException() {
    try {
        throw;
    }
    catch (const RuntimeError& e) {
        std::osyncstream out(err_.get());
        errorPrefix(out) << e.what() << "\n";
        errorPrefix(out) << e.details() << "\n";
    }
    catch (const std::exception& e) {
        error() << e.what() << "\n";
    }
    catch (...) {
        error() << "Unknown exception" << "\n";
    }
    exit(EXIT_FAILURE);
}

void Application::setExitCode(int n) { exitCode_ = n; }
int  Application::getExitCode() const { return exitCode_; }

// Called on application shutdown
void Application::shutdown(bool hasError) {
    // ignore signals/alarms during shutdown
    blockSignals();
    killAlarm();
    if (hasError) {
        onUnhandledException();
    }
    try {
        shutdown();
    }
    catch (...) {
        onUnhandledException();
    }
}

void Application::shutdown() {}

// Force exit without calling destructors.
void Application::exit(int exitCode) const {
    flush();
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
    if (blockSignals() == 0) {
        if (not onSignal(sig)) {
            return;
        } // block further signals
    }
    else if (pending_ == 0) { // signals are currently blocked because output is active
        info() << "Queueing signal...\n";
        pending_ = sig;
    }
    fetch_and_dec(blocked_);
}

bool Application::onSignal(int x) {
    info() << "INTERRUPTED by signal!\n";
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
    try {
        unsigned      help    = 0;
        bool          version = false;
        ParsedOptions parsed; // options found in command-line
        OptionContext allOpts(std::string("<").append(getName()).append(">"));
        HelpOpt       helpO = getHelpOption();
        if (helpO.second == 0) {
            error() << "Invalid help option!\n";
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
        std::osyncstream out(err_);
        errorPrefix(out) << e.what() << "\n";
        infoPrefix(out) << "Try '--help' for usage information\n";
        return false;
    }
    return true;
}

void Application::printHelp(std::ostream& os, const OptionContext& root) {
    os << getName() << " version " << getVersion() << "\n";
    printUsage(os);
    ProgramOptions::OptionPrinter printer(os);
    root.description(printer);
    os << "\n";
    printUsage(os);
    os << "Default command-line:\n" << getName() << " " << root.defaults(strlen(getName()) + 1) << "\n";
}

void Application::printHelp(const OptionContext& root) {
    std::osyncstream out(out_);
    printHelp(out, root);
}

void Application::printVersion() {
    std::osyncstream out(out_);
    printVersion(out);
}
void Application::printVersion(std::ostream& os) {
    os << getName() << " version " << getVersion() << "\nAddress model: " << (int) (sizeof(void*) * CHAR_BIT) << "\n";
}

void Application::printUsage() {
    std::osyncstream out(out_);
    printUsage(out);
}

void Application::printUsage(std::ostream& os) { os << "usage: " << getName() << " " << getUsage() << "\n"; }

unsigned Application::verbose() const { return verbose_; }
void     Application::setVerbose(unsigned v) { verbose_ = v; }

} // namespace Potassco
