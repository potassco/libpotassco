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
#include <sstream>
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
static Application* instance_s; // running instance (only valid during run()).
Application::Application()
    : exitCode_(EXIT_FAILURE)
    , timeout_(0)
    , verbose_(0)
    , fastExit_(false)
    , blocked_(0)
    , pending_(0) {}
Application::~Application() { resetInstance(*this); }
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
    POTASSCO_CHECK(alarmEvent != nullptr, std::errc::operation_not_supported, "Could not set time limit!");
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

// Application entry point.
int Application::main(int argc, char** argv) {
    initInstance(*this); // singleton instance used for signal handling
    exitCode_ = EXIT_FAILURE;
    blocked_  = 0;
    pending_  = 0;
    try {
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
            exitCode_       = EXIT_SUCCESS;
            auto exceptions = std::uncaught_exceptions();
            POTASSCO_SCOPE_EXIT({
                auto unwinding = std::uncaught_exceptions() > exceptions;
                try {
                    // ignore signals/alarms during shutdown
                    blockSignals();
                    killAlarm();
                    shutdown();
                }
                catch (...) {
                    if (not unwinding) {
                        throw; // propagate exception from shutdown
                    }
                    else {
                        // swallow additional exception from shutdown
                    }
                }
            }); // shutdown
            setup();
            run();
        }
    }
    catch (...) {
        char buffer[1024];
        exitCode_ = exitCode_ == EXIT_SUCCESS ? EXIT_FAILURE : exitCode_;
        auto ok   = formatActiveException(buffer);
        fastExit_ = onUnhandledException(buffer) || not ok;
    }
    if (fastExit_) {
        exit(exitCode_);
    }
    flush();
    return exitCode_;
}

Application* Application::getInstance() { return instance_s; }

void Application::setExitCode(int n) { exitCode_ = n; }
int  Application::getExitCode() const { return exitCode_; }

void Application::shutdown() {}

// Force exit without calling destructors.
void Application::exit(int exitCode) {
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
        pending_ = sig;
    }
    fetch_and_dec(blocked_);
}

bool Application::onSignal(int x) { exit(EXIT_FAILURE | (128 + x)); }

// Kill any pending alarm.
void Application::killAlarm() {
    if (timeout_ > 0) {
        setAlarm(0);
    }
}

static const char* prefix(Application::MessageType t) {
    switch (t) {
        default                     : return "<?>";
        case Application::MsgError  : return "*** ERROR: ";
        case Application::MsgWarning: return "*** Warn : ";
        case Application::MsgInfo   : return "*** Info : ";
    }
}

void Application::write(std::ostream& os, MessageType type, const char* msg) const {
    os << prefix(type) << "(" << getName() << "): " << (msg ? msg : "");
}
std::size_t Application::formatMessage(std::span<char> buffer, MessageType t, const char* fmt, ...) const {
    auto r1 = snprintf(buffer.data(), buffer.size(), "%s(%s): ", prefix(t), getName());
    if (r1 <= 0 || static_cast<std::size_t>(r1) >= buffer.size()) {
        return r1 <= 0 ? static_cast<std::size_t>(0) : buffer.size() - 1;
    }
    va_list args;
    va_start(args, fmt);
    auto rest = buffer.subspan(static_cast<std::size_t>(r1));
    auto r2   = std::vsnprintf(rest.data(), rest.size(), fmt, args);
    va_end(args);
    return r2 <= 0 ? static_cast<std::size_t>(0) : std::min(buffer.size() - 1, static_cast<std::size_t>(r1 + r2));
}

bool Application::formatActiveException(std::span<char> buffer) const {
    try {
        throw;
    }
    catch (const Potassco::ProgramOptions::Error& e) {
        buffer = buffer.subspan(formatMessage(buffer, MsgError, "%s\n", e.what()));
        formatMessage(buffer, MsgInfo, "Try '--help' for usage information");
    }
    catch (const Potassco::RuntimeError& e) {
        auto m = e.message();
        auto d = e.details();
        buffer = buffer.subspan(formatMessage(buffer, MsgError, "%.*s\n", (int) m.size(), m.data()));
        formatMessage(buffer, MsgError, "%.*s", (int) d.size(), d.data());
    }
    catch (const Application::Error& e) {
        buffer = buffer.subspan(formatMessage(buffer, MsgError, "%s%s", e.what(), not e.info.empty() ? "\n" : ""));
        if (not e.info.empty() && not buffer.empty()) {
            buffer = buffer.subspan(
                formatMessage(buffer, MsgInfo, "%s%s", e.info.c_str(), not e.details.empty() ? "\n" : ""));
        }
        if (not e.details.empty() && not buffer.empty()) {
            formatMessage(buffer, MsgInfo, "%s", e.details.c_str());
        }
    }
    catch (const std::exception& e) {
        formatMessage(buffer, MsgError, "%s", e.what());
    }
    catch (...) {
        formatMessage(buffer, MsgError, "Unknown exception");
        return false;
    }
    return true;
}

// Process command-line options.
bool Application::applyOptions(int argc, char** argv) {
    using namespace ProgramOptions;

    unsigned      help    = 0;
    bool          version = false;
    ParsedOptions parsed; // options found in command-line
    OptionContext allOpts(std::string("<").append(getName()).append(">"));
    HelpOpt       helpO = getHelpOption();
    OptionGroup   basic("Basic Options");
    if (helpO.second > 0) {
        Value* hv = helpO.second == 1
                        ? storeTo(help)->flag()
                        : storeTo(help,
                                  [maxV = helpO.second](const std::string& v, unsigned& out) {
                                      return Potassco::stringTo(v, out) == std::errc{} && out > 0 && out <= maxV;
                                  })
                              ->arg("<n>")
                              ->implicit("1");
        basic.addOptions()("help,h", hv, helpO.first);
    }
    basic.addOptions()                                                                                 //
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
        std::stringstream msg;
        msg << getName() << " version " << getVersion() << "\n";
        if (help) {
            auto x = (DescriptionLevel) (help - 1);
            allOpts.setActiveDescLevel(x);
            msg << "usage: " << getName() << " " << getUsage() << "\n";
            ProgramOptions::OptionPrinter printer(msg);
            allOpts.description(printer);
            msg << "\n";
            msg << "usage: " << getName() << " " << getUsage() << "\n";
            msg << "Default command-line:\n" << getName() << " " << allOpts.defaults(strlen(getName()) + 1) << "\n";
            onHelp(msg.str(), x);
        }
        else {
            msg << "Address model: " << (int) (sizeof(void*) * CHAR_BIT) << "-bit\n";
            onVersion(msg.str());
        }
        return false;
    }
    validateOptions(allOpts, parsed, values);
    return true;
}

unsigned Application::verbose() const { return verbose_; }
void     Application::setVerbose(unsigned v) { verbose_ = v; }

} // namespace Potassco
