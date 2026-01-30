//
// Copyright (c) 2024 - present, Benjamin Kaufmann
//
// This file is part of Potassco.
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
#include <potassco/platform.h>

#if __has_include(<fpu_control.h>)
#include <fpu_control.h>
#endif

#if __has_include(<io.h>)
#include <io.h>
#endif

#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

#if __has_include(<sys/time.h>)
#include <sys/time.h>
#endif

#if defined(_WIN32) && !defined(__EMSCRIPTEN__) && __has_include(<Windows.h>)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#if __has_include(<sys/resource.h>)
#include <sys/resource.h> // getrusage
#if defined(__APPLE__) && !defined(RUSAGE_THREAD)
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif
#endif

#include <cfloat>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <span>
#include <utility>

namespace PlatformApi {
///////////////////////////////////////////////////////////////////////////
// Alarm handling
///////////////////////////////////////////////////////////////////////////
#if defined(SIGALRM) && !defined(__EMSCRIPTEN__)
static auto orig_alarm_handler   = SIG_DFL;
static auto active_alarm_handler = static_cast<Potassco::AlarmFunc>(nullptr);
static auto setAlarm(uint32_t millis, Potassco::AlarmFunc func) -> std::errc {
    struct itimerval tv {};
    tv.it_value.tv_sec   = static_cast<long>(millis / 1000);
    tv.it_value.tv_usec  = static_cast<long>(millis % 1000) * 1000;
    active_alarm_handler = func;
    orig_alarm_handler   = signal(
        SIGALRM, +[](int) {
            signal(SIGALRM, orig_alarm_handler);
            if (auto h = std::exchange(active_alarm_handler, nullptr)) {
                h(SIGALRM);
            }
        });
    if (orig_alarm_handler != SIG_ERR && setitimer(ITIMER_REAL, &tv, nullptr) >= 0) {
        return {};
    }
    active_alarm_handler = nullptr;
    return static_cast<std::errc>(errno);
}
static bool killAlarm() {
    if (std::exchange(active_alarm_handler, nullptr)) {
        struct itimerval tv {};
        tv.it_value.tv_sec  = 0;
        tv.it_value.tv_usec = 0;
        setitimer(ITIMER_REAL, &tv, nullptr);
        signal(SIGALRM, orig_alarm_handler != SIG_ERR ? orig_alarm_handler : SIG_DFL);
        orig_alarm_handler = SIG_DFL;
        return true;
    }
    return false;
}
#elif defined(WINDOWS_LEAN_AND_MEAN)
static auto g_alarm_deleter = [](HANDLE h) { std::ignore = DeleteTimerQueueTimer(NULL, h, INVALID_HANDLE_VALUE); };
static auto g_alarm_handle  = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(g_alarm_deleter)>{};
static auto g_alarm_active  = false;
static auto setAlarm(uint32_t millis, Potassco::AlarmFunc f) -> std::errc {
    HANDLE h       = NULL;
    g_alarm_active = CreateTimerQueueTimer(
        &h, NULL,
        (WAITORTIMERCALLBACK) +
            [](PVOID func, BOOLEAN) {
                if (g_alarm_active) {
                    reinterpret_cast<Potassco::AlarmFunc>(func)(14);
                    g_alarm_active = false;
                }
            },
        f, millis, 0, 0);
    g_alarm_handle.reset(h);
    if (g_alarm_active) {
        return {};
    }
    return std::errc::operation_not_supported;
}
static bool killAlarm() {
    g_alarm_handle.reset();
    return std::exchange(g_alarm_active, false);
}
#else
static auto setAlarm(uint32_t, Potassco::AlarmFunc) -> std::errc { return std::errc::operation_not_supported; }
static bool killAlarm() { return false; }
#endif
///////////////////////////////////////////////////////////////////////////
// Timing stuff
///////////////////////////////////////////////////////////////////////////
#if defined(__EMSCRIPTEN__)
static auto getProcessTime() -> std::chrono::duration<double> {
    return std::chrono::duration<double>(std::numeric_limits<double>::quiet_NaN());
}
static auto getThreadTime() -> std::chrono::duration<double> { return getProcessTime(); }
#elif defined(RUSAGE_SELF)
using DurationType = std::chrono::microseconds;
static constexpr auto toDuration(const timeval& t) -> DurationType {
    return std::chrono::seconds(t.tv_sec) + std::chrono::microseconds(t.tv_usec);
}
static auto rusageTime(int who) -> DurationType {
    struct rusage usage = {};
    getrusage(who, &usage);
    return toDuration(usage.ru_utime) + toDuration(usage.ru_stime);
}
static auto getProcessTime() -> DurationType { return rusageTime(RUSAGE_SELF); }
static auto getThreadTime() -> DurationType {
    DurationType res{};
#if defined(RUSAGE_THREAD)
    res = rusageTime(RUSAGE_THREAD);
#elif __APPLE__
    struct thread_basic_info t_info;
    mach_msg_type_number_t   t_info_count = TASK_BASIC_INFO_COUNT;
    struct timeval           tv {};
    if (thread_info(mach_thread_self(), THREAD_BASIC_INFO, (thread_info_t) &t_info, &t_info_count) == KERN_SUCCESS) {
        time_value_add(&t_info.user_time, &t_info.system_time);
        tv.tv_sec  = static_cast<decltype(tv.tv_sec)>(t_info.user_time.seconds);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(t_info.user_time.microseconds);
        res        = toDuration(tv);
    }
#endif
    return res;
}
#elif defined(WINDOWS_LEAN_AND_MEAN)
using DurationType = std::chrono::duration<int64_t, std::ratio<1, std::nano::den / 100>>;
static DurationType toDuration(const FILETIME& t) {
    union Convert {
        FILETIME time;
        __int64  asUint;
    };
    return DurationType(Convert(t).asUint);
}
static auto getProcessTime() -> DurationType {
    FILETIME ignoreStart, ignoreExit, user, system;
    GetProcessTimes(GetCurrentProcess(), &ignoreStart, &ignoreExit, &user, &system);
    return toDuration(user) + toDuration(system);
}
static auto getThreadTime() -> DurationType {
    FILETIME ignoreStart, ignoreExit, user, system;
    GetThreadTimes(GetCurrentThread(), &ignoreStart, &ignoreExit, &user, &system);
    return toDuration(user) + toDuration(system);
}
#else
static auto getProcessTime() -> std::chrono::seconds { return std::chrono::seconds(0); }
static auto getThreadTime() -> std::chrono::seconds { return std::chrono::seconds(0); }
#endif
///////////////////////////////////////////////////////////////////////////
// FILE/Terminal stuff
///////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER) || defined(__MINGW32__)
static void lockfile(FILE* file) { _lock_file(file); }
static void unlockfile(FILE* file) { _unlock_file(file); }
static bool isTerminal(FILE* file) { return _isatty(_fileno(file)); }
static bool isCygPty(FILE* file) {
#if defined(WINDOWS_LEAN_AND_MEAN)
    auto h = (HANDLE) _get_osfhandle(_fileno(file));
    auto t = GetFileType(h);
    if (t == FILE_TYPE_PIPE) {
        union Info {
            FILE_NAME_INFO x;
            WCHAR          b[MAX_PATH + 1 + sizeof(FILE_NAME_INFO)];
        } info;
        if (not GetFileInformationByHandleEx(h, FileNameInfo, &info.x, sizeof(Info))) {
            return false;
        }
        info.x.FileName[info.x.FileNameLength] = 0;
        auto name                              = info.x.FileName;
        return (wcsstr(name, L"\\msys-") == name || wcsstr(name, L"\\cygwin-") == name) &&
               wcsstr(name, L"-pty") != nullptr;
    }
    return t == FILE_TYPE_CHAR;
#else
    return false;
#endif
}
#else
static bool isTerminal(FILE* file) { return isatty(fileno(file)) > 0; }
static bool isCygPty(FILE*) { return false; }
static void lockfile(FILE* file) { ::flockfile(file); }
static void unlockfile(FILE* file) { ::funlockfile(file); }
#endif

static auto enableTerminalColors([[maybe_unused]] FILE* file) -> std::errc {
    auto ec = std::errc::inappropriate_io_control_operation;
    if (isTerminal(file)) {
#if defined(__EMSCRIPTEN__)
        ec = std::errc::function_not_supported;
#elif !defined(_WIN32) || defined(__linux__)
        ec = {};
#elif defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
        if (file == stdout || file == stderr) {
            auto  hOut   = GetStdHandle(file == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            DWORD dwMode = 0;
            ec           = std::errc::function_not_supported;
            if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode) &&
                SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                ec = {};
            }
        }
#else
        ec = std::errc::function_not_supported;
#endif
    }
    else if (isCygPty(file)) {
        ec = {};
    }
    return ec;
}
///////////////////////////////////////////////////////////////////////////
// Floating point unit
///////////////////////////////////////////////////////////////////////////
#if FLT_EVAL_METHOD == 2 || FLT_EVAL_METHOD < 0
#if defined(_MSC_VER) || defined(_WIN32)
#pragma fenv_access(on)
#endif
static unsigned setFpuPrecision(unsigned* r) {
#if defined(_FPU_GETCW) && defined(_FPU_SETCW) && defined(_FPU_DOUBLE)
    fpu_control_t cw;
    _FPU_GETCW(cw);
    if (r && cw == static_cast<fpu_control_t>(*r)) {
        return *r;
    }
    auto nw = static_cast<fpu_control_t>(
        not r ? (cw & ~static_cast<fpu_control_t>(_FPU_EXTENDED) & ~static_cast<fpu_control_t>(_FPU_SINGLE)) |
                    static_cast<fpu_control_t>(_FPU_DOUBLE)
              : static_cast<fpu_control_t>(*r));
    _FPU_SETCW(nw);
    return static_cast<unsigned>(cw);
#elif defined(_MSC_VER) || defined(_WIN32)
    unsigned cw = _controlfp(0, 0);
    unsigned nw = not r ? static_cast<unsigned>(_PC_53) : *r;
    _controlfp(nw, _MCW_PC);
    return cw;
#else
    return UINT32_MAX;
#endif
    return 0u;
}
#else
constexpr unsigned setFpuPrecision(unsigned*) { return 0u; }
#endif
} // namespace PlatformApi
namespace Potassco {
static constexpr auto c_file = std::string_view{__FILE__};

auto initFpuPrecision() -> unsigned { return PlatformApi::setFpuPrecision(nullptr); }
void restoreFpuPrecision(unsigned r) { PlatformApi::setFpuPrecision(&r); }
bool isTerminal(FILE* file) { return PlatformApi::isTerminal(file); }
auto enableAnsiColorSupport(FILE* file) -> std::errc { return PlatformApi::enableTerminalColors(file); }
void lockFile(FILE* file) { PlatformApi::lockfile(file); }
void unlockFile(FILE* file) { PlatformApi::unlockfile(file); }
bool killAlarm() { return PlatformApi::killAlarm(); }
auto setAlarm(uint32_t millis, AlarmFunc f) -> std::errc {
    std::ignore = killAlarm();
    if (millis && f) {
        return PlatformApi::setAlarm(millis, f);
    }
    return std::errc::invalid_argument;
}
auto getProcessTime() -> double { return std::chrono::duration<double>(PlatformApi::getProcessTime()).count(); }
auto getThreadTime() -> double { return std::chrono::duration<double>(PlatformApi::getThreadTime()).count(); }

const char* ExpressionInfo::relativeFileName(const std::source_location& loc) {
    auto res = loc.file_name();
    for (auto cmp = c_file; *res && not cmp.empty() && *res == cmp.front(); cmp.remove_prefix(1), ++res) { ; }
    return res;
}

} // namespace Potassco
