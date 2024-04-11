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
    //! Output sink.
    using OutputSink = std::function<void(std::string_view)>;

    /*!
     * \name Basic functions.
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
    //! Returns the name of the option that should receive positional values or nullptr if not supported.
    [[nodiscard]] virtual const char* getPositional([[maybe_unused]] const std::string& value) const { return nullptr; }
    //! Prints the given error message to the error sink with prefix "*** ERROR: (getName()): ".
    POTASSCO_ATTRIBUTE_FORMAT(2, 3) void error(const char* msg, ...) const noexcept;
    //! Prints the given warning message to the error sink with prefix "*** Warn : (getName()): ".
    POTASSCO_ATTRIBUTE_FORMAT(2, 3) void warn(const char* msg, ...) const noexcept;
    //! Prints the given warning message to the error sink with prefix "*** Info : (getName()): ".
    POTASSCO_ATTRIBUTE_FORMAT(2, 3) void info(const char* msg, ...) const noexcept;
    //! Prints the given message to the output sink.
    POTASSCO_ATTRIBUTE_FORMAT(2, 3) void println(const char* msg, ...) const noexcept;

    //@}

    /*!
     * \name Main functions.
     */
    //@{
    void setOutputSink(OutputSink out);
    void setErrorSink(OutputSink err);

    //! Runs this application with the given command-line arguments.
    int main(int argc, char** argv);
    //! Sets the value that should be returned as the application's exit code.
    void setExitCode(int n);
    //! Returns the application's exit code.
    [[nodiscard]] int getExitCode() const;
    //! Returns the application object that is running.
    static Application* getInstance();
    //! Prints the application's help information (called if options contain '--help').
    virtual void printHelp(const ProgramOptions::OptionContext& root);
    //! Prints the application's version message (called if options contain '--version').
    virtual void printVersion();
    //! Prints the application's usage message (default is: "usage: getName() getUsage()").
    virtual void printUsage();
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
    //! Called once after option processing is done.
    virtual void setup() = 0;
    //! Shall run the application. Called after setup and option processing.
    virtual void run() = 0;
    //! Called after run returned. The default is a noop.
    virtual void shutdown();
    //! Called on an exception from run(). The default terminates the application.
    virtual void onUnhandledException();
    //! Called when a signal is received. The default terminates the application.
    virtual bool onSignal(int);
    //@}
protected:
    Application();
    virtual ~Application();
    [[nodiscard]] unsigned verbose() const;
    [[noreturn]] void      exit(int exitCode) const;

    void shutdown(bool hasError);
    void setVerbose(unsigned v);
    int  setAlarm(unsigned sec);
    void killAlarm();
    int  blockSignals();
    void unblockSignals(bool deliverPending);
    void processSignal(int sigNum);

private:
    bool        applyOptions(int argc, char** argv);
    static void initInstance(Application& app);
    static void resetInstance(Application& app);
    static void sigHandler(int sig);

    OutputSink          out_;       // standard output sink (defaults to stdout)
    OutputSink          err_;       // standard error sink (defaults to stderr)
    int                 exitCode_;  // application's exit code
    unsigned            timeout_;   // active time limit or 0 for no limit
    unsigned            verbose_;   // active verbosity level
    bool                fastExit_;  // force fast exit?
    volatile long       blocked_;   // temporarily block signals?
    volatile long       pending_;   // pending signal or 0 if no pending signal
    static Application* instance_s; // running instance (only valid during run()).
};

} // namespace Potassco
