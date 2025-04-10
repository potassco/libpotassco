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
#pragma once

#include <potassco/platform.h>
#include <potassco/program_opts/program_options.h>

#include <span>
#include <string>
#include <utility>
namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// Application base class
/////////////////////////////////////////////////////////////////////////////////////////
class Application {
public:
    //! Description of and max value for help option.
    using HelpOpt = std::pair<const char*, unsigned>;

    Application(Application&&) = delete;

    /*!
     * \name Query functions.
     */
    //@{
    //! Returns the name of this application.
    [[nodiscard]] virtual const char* getName() const = 0;
    //! Returns the version number of this application.
    [[nodiscard]] virtual const char* getVersion() const = 0;
    //! Returns a null-terminated array of signals that this application handles.
    [[nodiscard]] virtual const int* getSignals() const { return nullptr; }
    //! Returns the usage information of this application.
    [[nodiscard]] virtual const char* getUsage() const { return "[options]"; }
    //! Returns the application's help option and its description.
    [[nodiscard]] virtual HelpOpt getHelpOption() const { return {"Print help information and exit", 1}; }
    //! Returns the name of the option that should receive the given positional value or nullptr if not supported.
    [[nodiscard]] virtual const char* getPositional([[maybe_unused]] const std::string& value) const { return nullptr; }
    //@}

    /*!
     * \name Main functions.
     */
    //@{
    //! Runs this application with the given command-line arguments.
    int main(int argc, char** argv);
    //! Sets the value that should be returned as the application's exit code.
    void setExitCode(int n);
    //! Returns the application's exit code.
    [[nodiscard]] int getExitCode() const;
    //! Returns the application's current verbosity level.
    [[nodiscard]] unsigned getVerbose() const;
    //! Stops running application with given exit code and error message.
    /*!
     * The function sets the given code as exit code and then stops the running application by calling
     * Application::onUnhandledException() passing a formatted error message.
     * \note If the application is currently not running, this function is a noop.
     */
    void fail(int code, std::string_view message, std::string_view info = {});
    //! Stops running application with given exit code.
    /*!
     * The function sets the given code as exit code and then stops the running application.
     * \note If the application is currently not running, this function is a noop.
     */
    void stop(int code);
    //! Returns the application object that is running.
    static Application* getInstance();

    enum MessageType { message_error, message_warning, message_info };
    //! Writes a (null-terminated) message of given type to the provided buffer.
    /*!
     * The message format is: '***' <type-prefix> (<app-name>): <formatted-message>
     *
     * \param[out] buffer Buffer for storing the formatted message.
     * \param type Type of message.
     * \param fmt  A printf-style format string.
     * \param ...  Arguments matching the format string.
     * \return The number of bytes written not counting the null-terminator. If the message exceeds the given buffer,
     *         output is truncated but still null-terminated. If buffer.size() is 0, the function has no effect and
     *         returns 0.
     */
    std::size_t formatMessage(std::span<char> buffer, MessageType type, const char* fmt, ...) const
        POTASSCO_ATTRIBUTE_FORMAT(4, 5); // NOLINT

    //! Returns an io-manipulator that writes the given messages formatted as @c message_error to a stream.
    [[nodiscard]] auto error(const char* msg = "") const {
        return Prefix{.app = this, .msg = msg, .type = message_error};
    }
    //! Returns an io-manipulator that writes the given messages formatted as @c message_warning to a stream.
    [[nodiscard]] auto warn(const char* msg = "") const {
        return Prefix{.app = this, .msg = msg, .type = message_warning};
    }
    //! Returns an io-manipulator that writes the given messages formatted as @c message_info to a stream.
    [[nodiscard]] auto info(const char* msg = "") const {
        return Prefix{.app = this, .msg = msg, .type = message_info};
    }

    //@}
protected:
    /*!
     * \name Life cycle and option handling
     */
    //@{
    //! Adds all application options to the given context.
    virtual void initOptions(ProgramOptions::OptionContext& root) = 0;
    //! Validates parsed options. Shall throw to signal error.
    virtual void validateOptions(const ProgramOptions::OptionContext& root, const ProgramOptions::ParsedOptions& parsed,
                                 const ProgramOptions::ParsedValues& values) = 0;
    //! Shall print the provided help message.
    virtual void onHelp(const std::string& help, ProgramOptions::DescriptionLevel level) = 0;
    //! Shall print the provided version info.
    virtual void onVersion(const std::string& version) = 0;
    //! Called once after option processing is done.
    virtual void setup() = 0;
    //! Shall run the application. Called after setup and option processing.
    virtual void run() = 0;
    //! Called after run returned. Should not throw. The default is a noop.
    virtual void shutdown();
    //! Called on an active (i.e. unhandled) exception.
    /*!
     * The return value defines whether the application should exit immediately without calling destructors (true) or
     * just return from main() (false).
     */
    virtual bool onUnhandledException(const std::exception_ptr& e, const char* msg) noexcept = 0;
    //! Called when a signal is received. The default terminates the application.
    virtual bool onSignal(int);
    //! Shall write any pending application output. Always called before the Application terminates.
    virtual void flush() = 0;
    //@}

    Application();
    virtual ~Application();

    void setVerbose(unsigned v);
    void setAlarm(unsigned sec);
    void killAlarm();
    int  blockSignals();
    void unblockSignals(bool deliverPending);
    void processSignal(int sigNum);

private:
    struct Error;
    struct Stop;
    struct Prefix {
        const Application* app;
        const char*        msg;
        MessageType        type;
    };
    friend std::ostream& operator<<(std::ostream& os, const Prefix& p) {
        p.app->write(os, p.type, p.msg);
        return os;
    }
    void              write(std::ostream& os, MessageType type, const char* msg) const;
    bool              applyOptions(int argc, char** argv);
    void              handleException();
    static void       initInstance(Application& app);
    static void       resetInstance(const Application& app);
    static void       sigHandler(int sig);
    [[noreturn]] void exit(int exitCode);

    int      exitCode_; // application's exit code
    unsigned timeout_;  // active time limit or 0 for no limit
    unsigned verbose_;  // active verbosity level
    bool     fastExit_; // force fast exit?
    int      blocked_;  // temporarily block signals?
    int      pending_;  // pending signal or 0 if no pending signal
};

} // namespace Potassco
