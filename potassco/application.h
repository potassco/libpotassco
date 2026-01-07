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

#include <potassco/format.h>
#include <potassco/program_opts/program_options.h>

#include <span>
#include <string>

namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// Application base class
/////////////////////////////////////////////////////////////////////////////////////////
class Application {
public:
    //! Default color style for messages if enabled via `enableColoredMessages()`.
    static constexpr TextStyle col_error   = TextStyle::Emphasis::bold | TextStyle::Color::red;
    static constexpr TextStyle col_warning = TextStyle::Emphasis::bold | TextStyle::Color::bright_yellow;
    static constexpr TextStyle col_info    = TextStyle::Emphasis::bold | TextStyle::Color::cyan;
    static constexpr TextStyle col_em      = TextStyle::Emphasis::bold;
    //! Default color style for help output if enabled via `enableColoredHelp()`.
    static constexpr TextStyle col_usage     = col_em;
    static constexpr TextStyle col_program   = TextStyle::Emphasis::bold | TextStyle::Color::bright_yellow;
    static constexpr TextStyle col_def_cmd   = TextStyle::Color::bright_magenta;
    static constexpr TextStyle col_opt_group = TextStyle::Color::bright_blue | TextStyle::Emphasis::bold;
    static constexpr TextStyle col_opt_short = TextStyle::Color::green | TextStyle::Emphasis::bold;
    static constexpr TextStyle col_opt_long  = TextStyle::Emphasis::bold | TextStyle::Color::cyan;
    static constexpr TextStyle col_opt_arg   = TextStyle::Emphasis::bold | TextStyle::Color::yellow;

    //! Description of and max value for the help option.
    struct HelpOpt {
        HelpOpt(ProgramOptions::Str str, unsigned lev) : desc(str), max(lev) {}
        ProgramOptions::Str desc; //!< Description of the help option.
        unsigned            max;  //!< Max supported value or 0 if option should not be added.
    };
    //! Range and default value for the verbose option.
    struct VerboseOpt {
        VerboseOpt(ProgramOptions::Str defVal, unsigned maxLev = UINT32_MAX) : def(defVal), max(maxLev) {}
        ProgramOptions::Str def; //!< Default value
        unsigned            max; //!< Max supported value or 0 if option should not be added.
    };

    Application(Application&&) = delete;

    /*!
     * \name Query functions.
     */
    //@{
    //! Returns the name of this application.
    [[nodiscard]] virtual std::string_view getName() const = 0;
    //! Returns the version number of this application.
    [[nodiscard]] virtual std::string_view getVersion() const = 0;
    //! Returns the list of signals this application wants to handle.
    [[nodiscard]] virtual std::span<const int> getSignals() const { return {}; }
    //! Returns the usage information of this application.
    [[nodiscard]] virtual std::string_view getUsage() const { return "[options]"; }
    //! Returns the application's help option and its description.
    [[nodiscard]] virtual HelpOpt getHelpOption() const { return {"Print help information and exit", 1}; }
    //! Returns details about the application's verbose option.
    [[nodiscard]] virtual VerboseOpt getVerboseOption() const { return {""}; }
    //! Returns the name of the option that should receive the given positional value or an empty view if not supported.
    [[nodiscard]] virtual std::string_view getPositional([[maybe_unused]] std::string_view value) const { return {}; }
    //@}

    /*!
     * \name Main functions.
     */
    //@{
    //! Runs this application with the given command-line arguments.
    int main(std::span<const char* const> args);
    //! Runs this application with the given command-line arguments, skipping the first argument.
    int main(int argc, char** argv);
    //! Sets the value that should be returned as the application's exit code.
    void setExitCode(int n);
    //! Returns the application's exit code.
    [[nodiscard]] int getExitCode() const;
    //! Returns the application's current verbosity level.
    [[nodiscard]] unsigned getVerbose() const;
    //! Returns the current time limit in milliseconds or 0 if no time limit is set.
    [[nodiscard]] unsigned getTimeLimit() const;
    //! Stops running application with the given exit code and error message.
    /*!
     * The function sets the given code as exit code and then stops the running application by calling
     * Application::onUnhandledException() passing a formatted error message.
     * \note If the application is currently not running, this function is a noop.
     */
    void fail(int code, std::string_view message, std::string_view info = {});
    //! Stops running application with the given exit code.
    /*!
     * The function sets the given code as exit code and then stops the running application.
     * \note If the application is currently not running, this function is a noop.
     */
    void stop(int code);
    //! Returns the application object that is running.
    static Application* getInstance();

    enum MessageType { message_error, message_warning, message_info };

    //! Returns an io-manipulator that writes the given message of type `t` to a stream.
    /*!
     * The message format is: [col_<type>]'***' <type-prefix> (<app-name>): [<reset>col_em]<message>[<reset>], where
     *  the optional color codes are only added if colored messages are enabled.
     *
     * \param type Type of message.
     * \param msg The message to write.
     * \param exception Apply additional exception message formatting.
     */
    [[nodiscard]] auto message(MessageType type, std::string_view msg = {}, bool exception = false) const {
        return Prefix{.app = this, .msg = msg, .level = type, .exception = exception};
    }
    //! Returns an io-manipulator that writes the given messages formatted as `message_error` to a stream.
    [[nodiscard]] auto error(std::string_view msg = {}) const { return message(message_error, msg); }
    //! Returns an io-manipulator that writes the given messages formatted as `message_warning` to a stream.
    [[nodiscard]] auto warn(std::string_view msg = {}) const { return message(message_warning, msg); }
    //! Returns an io-manipulator that writes the given messages formatted as `message_info` to a stream.
    [[nodiscard]] auto info(std::string_view msg = {}) const { return message(message_info, msg); }

    //! Enables formatting of messages with ansi colors.
    /*!
     * If enabled, messages are formatted with ansi color codes.
     * Error, Warning, and Info messages are prefixed with col_error, col_warning, and col_info, respectively.
     * Additionally, any provided message is highlighted in col_em.
     */
    void enableColoredMessages(bool enable = true);
    //! Enables formatting of help text with ansi colors.
    void enableColoredHelp(bool enable = true);

    //@}
protected:
    /*!
     * \name Life cycle and option handling
     */
    //@{
    //! Adds all application options to the given context.
    virtual void initOptions(ProgramOptions::OptionContext& root) = 0;
    //! Validates parsed options. Shall throw to signal error.
    virtual void validateOptions(const ProgramOptions::OptionContext& root,
                                 const ProgramOptions::ParsedOptions& parsed) = 0;
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
    //! Called on an active (i.e., unhandled) exception.
    /*!
     * The return value defines whether the application should exit immediately without calling destructors (true) or
     * just return from main() (false).
     */
    virtual bool onUnhandledException(const std::exception_ptr& e, std::string_view msg) noexcept = 0;
    //! Called when a signal is received. The default terminates the application.
    virtual bool onSignal(int);
    //! Shall write any pending application output. Always called before the Application terminates.
    virtual void flush() = 0;
    //@}

    Application();
    virtual ~Application();
    [[nodiscard]] bool hasColoredMessages() const { return colorMsg_; }
    [[nodiscard]] bool hasColoredHelp() const { return colorHelp_; }

    void setVerbose(unsigned v);
    void setAlarm(unsigned sec);
    void setAlarmMs(unsigned millis);
    void killAlarm();
    int  blockSignals();
    void unblockSignals(bool deliverPending);
    void processSignal(int sigNum);

private:
    using Sink = ProgramOptions::OutputSink;
    struct Stop;
    struct Prefix {
        const Application* app;
        std::string_view   msg;
        MessageType        level;
        uint8_t            exception;
    };
    friend std::ostream& operator<<(std::ostream& os, const Prefix& p) {
        p.app->write(Sink{os}, p);
        return os;
    }
    template <CharBuffer B>
    friend B& operator<<(B& b, const Prefix& p) {
        p.app->write(Sink{b}, p);
        return b;
    }
    void              write(Sink s, const Prefix& p) const;
    static Sink&      colorize(Sink& sink, std::string_view msg, const TextStyle& color, bool exception);
    bool              applyOptions(std::span<const char* const> args);
    void              handleException();
    bool              unhandledException(const std::exception_ptr& e, std::string_view error, std::string_view info);
    static void       initInstance(Application& app);
    static void       resetInstance(const Application& app);
    static void       sigHandler(int sig);
    [[noreturn]] void fastExit(int exitCode);

    int      exitCode_;  // application's exit code
    unsigned timeout_;   // active time limit or 0 for no limit
    unsigned verbose_;   // active verbosity level
    bool     fastExit_;  // force fast exit?
    int      blocked_;   // temporarily block signals?
    int      pending_;   // pending signal or 0 if no pending signal
    bool     colorMsg_;  // format messages with ansi colors?
    bool     colorHelp_; // format help with ansi colors?
};

} // namespace Potassco
