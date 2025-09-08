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

#include <potassco/basic_types.h>
#include <potassco/error.h>
#include <potassco/program_opts/errors.h>
#include <potassco/program_opts/typed_value.h>

#include <atomic>
#include <climits>
#include <csignal>
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
struct Application::Stop final : std::exception {};

static Application* g_instance; // running instance (only valid during run()).

Application::Application()
    : exitCode_(EXIT_FAILURE)
    , timeout_(0)
    , verbose_(0)
    , fastExit_(false)
    , blocked_(0)
    , pending_(0)
    , color_(false) {}
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
int Application::main(std::span<const char* const> args) {
    initInstance(*this); // singleton instance used for signal handling
    exitCode_ = EXIT_FAILURE;
    blocked_  = 0;
    pending_  = 0;
    try {
        if (applyOptions(args)) {
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
        handleException();
    }
    if (fastExit_) {
        exit(exitCode_);
    }
    flush();
    return exitCode_;
}
int Application::main(int argc, char** argv) {
    POTASSCO_CHECK_PRE(argc >= 0, "invalid arg count");
    POTASSCO_CHECK_PRE(argc == 0 || argv != nullptr, "invalid arg vector");
    auto sz = static_cast<std::size_t>(argc);
    return main(std::span<const char* const>(argv, sz).subspan(sz > 0));
}

Application* Application::getInstance() { return g_instance; }

static constexpr auto col_reset = "\033[0m"sv;
static constexpr auto col_em    = "\033[1m"sv;
static constexpr auto col_err   = "\033[1;31m"sv;
static constexpr auto col_warn  = "\033[1;93m"sv;
static constexpr auto col_info  = "\033[1;36m"sv;

static std::string_view color(Application::MessageType t) {
    switch (t) {
        default                          : return ""sv;
        case Application::message_error  : return col_err;
        case Application::message_warning: return col_warn;
        case Application::message_info   : return col_info;
    }
}
void Application::handleException() {
    auto             current = std::current_exception();
    int              code    = EXIT_FAILURE;
    std::string_view error;
    std::string_view info;
    try {
        throw;
    }
    catch (const ProgramOptions::Error& e) {
        error = e.what();
        info  = "Try '--help' for usage information";
    }
    catch (const RuntimeError& e) {
        error = e.message();
        info  = e.details();
    }
    catch (const Stop&) {
        code = EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        error = std::string_view{e.what()};
        if (auto p = error.find('\n'); p != std::string_view::npos) {
            info  = error.substr(p + 1);
            error = error.substr(0, p);
        }
    }
    catch (...) {
        error     = "Unknown exception";
        fastExit_ = true;
    }
    exitCode_ = exitCode_ == EXIT_SUCCESS ? code : exitCode_;
    if (code != EXIT_SUCCESS && unhandledException(current, error, info)) {
        fastExit_ = true;
    }
    if (fastExit_) {
        exit(exitCode_);
    }
}
bool Application::unhandledException(const std::exception_ptr& e, std::string_view error, std::string_view info) {
    char          local[1024];
    DynamicBuffer buffer(local);
    write(Sink{buffer}, message_error, error, true);
    if (not info.empty()) {
        write(Sink{buffer.append("\n"sv)}, message_info, info, true);
    }
    return onUnhandledException(e, buffer.view());
}
void Application::setExitCode(int n) { exitCode_ = n; }
int  Application::getExitCode() const { return exitCode_; }
void Application::fail(int code, std::string_view message, std::string_view info) {
    if (this == getInstance()) {
        if (not fastExit_) {
            setExitCode(code);
            throw std::runtime_error(std::string{message}.append(not info.empty(), '\n').append(info));
        }
        std::ignore = unhandledException(nullptr, message, info);
        Application::exit(code);
    }
}
void Application::stop(int code) {
    if (this == getInstance()) {
        if (not fastExit_) {
            setExitCode(code);
            throw Stop();
        }
        Application::exit(code);
    }
}
void Application::enableColoredMessages(bool col) { color_ = col; }

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
        try {
            auto fast = std::exchange(fastExit_, true);
            POTASSCO_SCOPE_EXIT({ fastExit_ = fast; });
            if (not onSignal(sigNum)) {
                return; // block further signals
            }
        }
        catch (...) {
            handleException();
            exit(exitCode_);
        }
    }
    else if (pending_ == 0) { // signals are currently blocked because output is active
        pending_ = sigNum;
    }
    fetchDec(blocked_);
}

bool Application::onSignal(int x) { exit(128 + x); }

static std::string_view prefix(Application::MessageType t) {
    switch (t) {
        default                          : return "<?>"sv;
        case Application::message_error  : return "*** ERROR: "sv;
        case Application::message_warning: return "*** Warn : "sv;
        case Application::message_info   : return "*** Info : "sv;
    }
}

auto Application::colorize(Sink& sink, std::string_view msg, std::string_view color, bool internal) -> Sink& {
    if (color.empty() || msg.empty()) {
        return sink.write(msg);
    }
    if (not internal) {
        return sink.write(color).write(msg).write(col_reset);
    }
    static constexpr auto pre_key      = "Precondition "sv;
    static constexpr auto check_key    = "check "sv;
    static constexpr auto post_key     = "' failed."sv;
    auto                  isExpression = [](std::string_view in, std::size_t p, std::string_view key) {
        if (p >= key.size() && in.substr(p - key.size()).starts_with(key)) {
            return in.find(post_key, p + 1);
        }
        return std::string_view::npos;
    };
    while (not msg.empty()) {
        auto p = msg.find('\'');
        if (auto e = msg.find('\'', p + (p != std::string_view::npos));
            p == std::string_view::npos || e == std::string_view::npos) {
            sink.write(msg);
            msg = {};
        }
        else if (auto exp = isExpression(msg, p, pre_key); exp != std::string_view::npos) {
            sink.write(msg.substr(0, p - pre_key.size())).write(col_warn).write(pre_key).write(col_reset).write("'"sv);
            ++p;
            sink.write(color).write(msg.substr(p, exp - p)).write(col_reset).write("' "sv);
            sink.write(col_warn).write(post_key.substr(2)).write(col_reset);
            msg = msg.substr(exp + post_key.size());
        }
        else {
            exp = isExpression(msg, p, check_key);
            e   = exp != std::string_view::npos ? exp : e;
            ++p;
            sink.write(msg.substr(0, p)).write(color).write(msg.substr(p, e - p)).write(col_reset);
            sink.write("'"sv);
            msg = msg.substr(e + 1);
        }
    }
    return sink;
}

void Application::write(Sink s, MessageType type, std::string_view msg, bool internal) const {
    std::string_view col[2];
    std::string_view post;
    std::string_view sep{};
    if (color_) {
        col[0] = color(type);
        col[1] = not msg.empty() ? col_em : ""sv;
        post   = not col[0].empty() ? col_reset : ""sv;
    }
    do {
        auto line = internal ? msg.substr(0, std::min(msg.find('\n'), msg.size())) : msg;
        s.write(sep).write(col[0]).write(prefix(type)).write("("sv).write(getName()).write("): "sv).write(post);
        colorize(s, line, col[1], internal);
        sep = "\n"sv;
        msg.remove_prefix(std::min(line.size() + 1, msg.size()));
    } while (not msg.empty());
}

// Process command-line options.
bool Application::applyOptions(std::span<const char* const> args) {
    using namespace ProgramOptions;

    unsigned help         = 0;
    std::ignore           = std::addressof(help); // Disable false positive from CppDFAUnreachableCode analysis
    bool          version = false;
    OptionContext allOpts(std::string("<").append(getName()).append(">"));
    OptionGroup   basic("Basic Options");
    auto          init = basic.addOptions();
    if (auto [message, level] = getHelpOption(); level > 0) {
        auto hv = level == 1 ? storeTo(help).flag()
                             : storeTo(help,
                                       [maxV = level](std::string_view v, unsigned& out) {
                                           return stringTo(v, out) == std::errc{} && out > 0 && out <= maxV;
                                       })
                                   .arg("<n>")
                                   .implicit("1");
        init("-h,help", std::move(hv), message);
    }
    verbose_ = 0;
    if (auto [def, max] = getVerboseOption(); max > 0) {
        auto opt = storeTo(verbose_, [maxV = max](std::string_view v, unsigned& out) {
            if (v == "umax") {
                out = maxV;
                return true;
            }
            return stringTo(v, out) == std::errc{} && out <= maxV;
        });
        opt.arg("<n>").implicit("umax");
        if (not def.empty()) {
            opt.defaultsTo(def);
        }
        init("-V,verbose", std::move(opt), "Set verbosity level to %A");
    }
    init("-v,version", flag(version), "Print version information and exit")                           //
        ("time-limit", storeTo(timeout_ = 0).arg("<n>"), "Set time limit to %A seconds (0=no limit)") //
        ("@1,fast-exit", flag(fastExit_ = false), "Force fast exit (do not call dtors)");             //
    allOpts.add(std::move(basic));
    initOptions(allOpts);
    DefaultParseContext parseContext{allOpts};
    parseCommandArray(parseContext, args, [this](std::string_view value, std::string& opt) {
        if (auto n = getPositional(value); not n.empty()) {
            opt = n;
            return true;
        }
        return false;
    });
    allOpts.assignDefaults(parseContext.parsed());
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
            msg << "Default command-line:\n" << getName() << " " << allOpts.defaults(getName().size() + 1);
            onHelp(msg.str(), x);
        }
        else {
            msg << "Address model: " << static_cast<int>(sizeof(void*) * CHAR_BIT) << "-bit";
            onVersion(msg.str());
        }
        return false;
    }
    validateOptions(allOpts, parseContext.parsed());
    return true;
}

unsigned Application::getVerbose() const { return verbose_; }
void     Application::setVerbose(unsigned v) { verbose_ = v; }

} // namespace Potassco
