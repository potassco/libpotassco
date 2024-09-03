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

#include <atomic>
#include <climits>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if __has_include(<unistd.h>)
#include <unistd.h> // for _exit
#endif

using AlarmHandler = void (*)(int);

#if not defined(SIGALRM)
#include <condition_variable>
#include <thread>
static AlarmHandler g_alarmHandler = SIG_DFL;
static void         setAlarmHandler(AlarmHandler handler) { g_alarmHandler = handler; }
static void         alarm(unsigned sec) {
    static std::jthread alarmThread;
    if (alarmThread.joinable()) {
        alarmThread.request_stop();
        alarmThread.join();
    }
    if (sec) {
        alarmThread = std::jthread([timeout = std::chrono::seconds(sec)](const std::stop_token& stop) {
            std::condition_variable_any cond;
            std::mutex                  m;
            m.lock();
            cond.wait_for(m, timeout, []() { return false; });
            if (not stop.stop_requested() && g_alarmHandler != SIG_IGN && g_alarmHandler != SIG_DFL) {
                g_alarmHandler(14);
            }
        });
    }
}
#else
static void setAlarmHandler(AlarmHandler handler) { signal(SIGALRM, handler); }
#endif

using namespace Potassco::ProgramOptions;
using namespace std;
namespace Potassco {
template <typename T>
static T fetchAndAdd(T* data, T add) {
    if constexpr (requires { __atomic_fetch_add(data, add, 0); }) {
#if !defined(__ATOMIC_ACQ_REL)
#define __ATOMIC_ACQ_REL 4
#endif
        return __atomic_fetch_add(data, add, __ATOMIC_ACQ_REL);
    }
    else {
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
        return std::atomic_ref{*data}.fetch_add(add);
#else
        static_assert(std::is_same_v<T, void>, "unsupported compuler");
#endif
    }
}

template <typename T>
static int fetchInc(T& x) {
    return fetchAndAdd(&x, static_cast<T>(1));
}
template <typename T>
static int fetchDec(T& x) {
    return fetchAndAdd(&x, static_cast<T>(-1));
}
/////////////////////////////////////////////////////////////////////////////////////////
// Application
/////////////////////////////////////////////////////////////////////////////////////////
static Application* g_instance; // running instance (only valid during run()).
struct Application::Error : std::runtime_error {
    explicit Error(const char* msg) : std::runtime_error(msg) {}
};
Application::Application()
    : exitCode_(EXIT_FAILURE)
    , timeout_(0)
    , verbose_(0)
    , fastExit_(false)
    , blocked_(0)
    , pending_(0) {}
Application::~Application() { resetInstance(*this); }
void Application::initInstance(Application& app) { g_instance = &app; }
void Application::resetInstance(const Application& app) {
    if (g_instance == &app) {
        g_instance = nullptr;
    }
}

void Application::setAlarm(unsigned sec) {
    if (sec) {
        setAlarmHandler(&Application::sigHandler);
    }
    timeout_ = sec;
    alarm(sec);
}

// Kill any pending alarm.
void Application::killAlarm() {
    if (std::exchange(timeout_, 0u) > 0u) {
        setAlarmHandler(SIG_DFL);
        alarm(0);
    }
}

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
                    // swallow additional exception from shutdown
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

Application* Application::getInstance() { return g_instance; }

void Application::setExitCode(int n) { exitCode_ = n; }
int  Application::getExitCode() const { return exitCode_; }
void Application::fail(int code, std::string_view message, std::string_view info) {
    if (this == getInstance()) {
        char  mem[1024];
        auto* pos  = std::begin(mem);
        auto* end  = std::end(mem);
        pos       += formatMessage(mem, message_error, "%.*s", static_cast<int>(message.length()), message.data());
        while (not info.empty() && (end - pos) > 1) {
            auto line  = info.substr(0, std::min(info.find('\n'), info.size()));
            *pos++     = '\n';
            pos       += formatMessage({pos, end}, message_info, "%.*s", static_cast<int>(line.length()), line.data());
            info.remove_prefix(std::min(info.size(), line.size() + 1));
        }
        if (not fastExit_) {
            setExitCode(code);
            throw Error(mem);
        }
        std::ignore = onUnhandledException(mem);
        Application::exit(code);
    }
}

void Application::shutdown() {}

// Force exit without calling destructors.
void Application::exit(int exitCode) {
    flush();
    _exit(exitCode);
}

// Temporarily disable delivery of signals.
int Application::blockSignals() { return fetchInc(blocked_); }

// Re-enable signal handling and deliver any pending signal.
void Application::unblockSignals(bool deliverPending) {
    if (fetchDec(blocked_) == 1) {
        // directly deliver any pending signal to our sig handler
        if (auto pend = std::exchange(pending_, 0); pend && deliverPending) {
            processSignal(pend);
        }
    }
}
void Application::sigHandler(int sig) {
    // On Windows and original Unix, a handler once invoked is set to SIG_DFL.
    // Instead, we temporarily ignore signals and reset our handler once it is done.
    auto restore = signal(sig, SIG_IGN);
    if (auto inst = getInstance()) {
        inst->processSignal(sig);
        restore = &Application::sigHandler;
    }
    signal(sig, restore);
}

// Called on timeout or signal.
void Application::processSignal(int sigNum) {
    if (blockSignals() == 0) {
        if (not onSignal(sigNum)) {
            return;
        } // block further signals
    }
    else if (pending_ == 0) { // signals are currently blocked because output is active
        pending_ = sigNum;
    }
    fetchDec(blocked_);
}

bool Application::onSignal(int x) { exit(EXIT_FAILURE | (128 + x)); }

static const char* prefix(Application::MessageType t) {
    switch (t) {
        default                          : return "<?>";
        case Application::message_error  : return "*** ERROR: ";
        case Application::message_warning: return "*** Warn : ";
        case Application::message_info   : return "*** Info : ";
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
    catch (const ProgramOptions::Error& e) {
        buffer      = buffer.subspan(formatMessage(buffer, message_error, "%s\n", e.what()));
        std::ignore = formatMessage(buffer, message_info, "Try '--help' for usage information");
    }
    catch (const RuntimeError& e) {
        auto m = e.message();
        auto d = e.details();
        buffer = buffer.subspan(formatMessage(buffer, message_error, "%.*s\n", static_cast<int>(m.size()), m.data()));
        formatMessage(buffer, message_error, "%.*s", static_cast<int>(d.size()), d.data());
    }
    catch (const Error& e) {
        snprintf(buffer.data(), buffer.size(), "%s", e.what());
    }
    catch (const std::exception& e) {
        std::ignore = formatMessage(buffer, message_error, "%s", e.what());
    }
    catch (...) {
        std::ignore = formatMessage(buffer, message_error, "Unknown exception");
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
            auto x = static_cast<DescriptionLevel>(help - 1);
            allOpts.setActiveDescLevel(x);
            msg << "usage: " << getName() << " " << getUsage() << "\n";
            OptionPrinter printer(msg);
            allOpts.description(printer);
            msg << "\n";
            msg << "usage: " << getName() << " " << getUsage() << "\n";
            msg << "Default command-line:\n" << getName() << " " << allOpts.defaults(strlen(getName()) + 1);
            onHelp(msg.str(), x);
        }
        else {
            msg << "Address model: " << static_cast<int>(sizeof(void*) * CHAR_BIT) << "-bit";
            onVersion(msg.str());
        }
        return false;
    }
    validateOptions(allOpts, parsed, values);
    return true;
}

unsigned Application::getVerbose() const { return verbose_; }
void     Application::setVerbose(unsigned v) { verbose_ = v; }

} // namespace Potassco
