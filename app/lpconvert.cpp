//
// Copyright (c) 2015 - present, Benjamin Kaufmann
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

#include <potassco/application.h>
#include <potassco/aspif.h>
#include <potassco/aspif_text.h>
#include <potassco/convert.h>
#include <potassco/error.h>
#include <potassco/smodels.h>

#include <potassco/program_opts/errors.h>
#include <potassco/program_opts/typed_value.h>

#include <cctype>
#include <fstream>
#include <iostream>

using namespace Potassco::ProgramOptions;

class LpConvert : public Potassco::Application {
public:
    [[nodiscard]] const char* getName() const override { return "lpconvert"; }
    [[nodiscard]] const char* getVersion() const override { return "2.0.0"; }
    [[nodiscard]] const char* getPositional(std::string_view) const override { return "input"; }
    [[nodiscard]] const char* getUsage() const override {
        return "[options] [<file>]\n"
               "Convert program in <file> or standard input";
    }
    void initOptions(OptionContext& root) override;
    void validateOptions(const OptionContext& ctx, const ParsedOptions& parsed) override {
        if (parsed.contains("text") && parsed.contains("format")) {
            throw Potassco::ProgramOptions::Error("options 'text' and 'format' are mutually exclusive");
        }
    }
    void setup() override {}
    void run() override;
    void onHelp(const std::string& info, Potassco::ProgramOptions::DescriptionLevel) override {
        std::cout << info << "\n";
    }
    void onVersion(const std::string& info) override {
        std::cout << info << "\nlibpotassco version " << LIB_POTASSCO_VERSION
                  << "\nCopyright (C) Benjamin Kaufmann\n"
                     "License: The MIT License <https://opensource.org/licenses/MIT>\n";
    }
    bool onUnhandledException(const std::exception_ptr&, const char* msg) noexcept override {
        std::cerr << msg << "\n";
        return false;
    }
    void flush() override {
        std::cout.flush();
        std::cerr.flush();
    }
    void handleException(std::string_view error) {
        auto sp   = error.find('\n');
        auto info = sp != std::string_view::npos ? error.substr(sp + 1) : std::string_view{};
        error     = error.substr(0, sp);
        if (auto unsupported = error.find("not supported") != std::string_view::npos;
            unsupported && format_ == Format::smodels) {
            error = error.substr(0, error.rfind(':'));
            info  = "Try different format or enable potassco extensions";
        }
        fail(EXIT_FAILURE, error, info);
    }

    enum class Format : unsigned { auto_, text, smodels, aspif_v1, aspif };

private:
    std::string input_;
    std::string output_;
    std::string pred_;
    Format      format_{Format::auto_};
    bool        potassco_ = false;
    bool        filter_   = false;
};
POTASSCO_SET_ENUM_ENTRIES(LpConvert::Format, {auto_, "auto"sv}, {text, "text"sv}, {smodels, "smodels"sv},
                          {aspif_v1, "aspif-v1"sv}, {aspif, "aspif"sv});

void LpConvert::initOptions(OptionContext& root) {
    OptionGroup convert("Conversion Options");
    convert.addOptions()                                                                                          //
        ("-i@2,input", storeTo(input_, std::string()), "Input file")                                              //
        ("-p,potassco", flag(potassco_, false), "Enable potassco extensions")                                     //
        ("-f,filter", flag(filter_, false), "Hide converted potassco predicates")                                 //
        ("-o,output", storeTo(output_, std::string())->arg("<file>"), "Write output to <file> (default: stdout)") //
        ("format", storeTo(format_, Format::auto_), "Output format (text|smodels|aspif|aspif-v1)")                //
        ("-t,text", flag([this](bool) { format_ = Format::text; }), "Convert to ground text format")              //
        ("aux-pred", storeTo(pred_, std::string()), "Prefix/Predicate for atom numbers in text output")           //
        ;
    root.add(convert);
}
void LpConvert::run() try {
    std::ifstream iFile;
    std::ofstream oFile;
    if (not input_.empty() && input_ != "-") {
        iFile.open(input_.c_str());
        POTASSCO_CHECK(iFile.is_open(), std::errc::no_such_file_or_directory, "Could not open input file");
    }
    if (not output_.empty() && output_ != "-") {
        POTASSCO_CHECK(input_ != output_, std::errc::invalid_argument, "Input and output must be different");
        oFile.open(output_.c_str());
        POTASSCO_CHECK(oFile.is_open(), std::errc::no_such_file_or_directory, "Could not open output file");
    }
    std::istream& in = iFile.is_open() ? iFile : std::cin;
    std::ostream& os = oFile.is_open() ? oFile : std::cout;
    POTASSCO_CHECK(in.peek() == 'a' || std::isdigit(in.peek()), std::errc::not_supported,
                   "Unrecognized input format '%c' - expected 'aspif' or <digit>", in.peek());
    Potassco::SmodelsInput::Options opts;
    if (potassco_) {
        opts.enableClaspExt().convertEdges().convertHeuristic();
        if (filter_) {
            opts.dropConverted();
        }
    }
    if (format_ == Format::auto_ && in.peek() == 'a') {
        format_ = Format::smodels;
    }
    std::unique_ptr<Potassco::AbstractProgram> out1;
    std::unique_ptr<Potassco::AbstractProgram> out2;

    switch (format_) {
        case Format::text: {
            auto text = std::make_unique<Potassco::AspifTextOutput>(os);
            if (not pred_.empty()) {
                try {
                    text->setAtomPred(pred_);
                }
                catch (const std::invalid_argument&) {
                    fail(EXIT_FAILURE, "invalid aux predicate: '" + pred_ + "'",
                         "atom prefix (e.g. 'x_') or unary predicate (e.g. '_id/1') expected");
                }
            }
            out1 = std::move(text);
            break;
        }
        case Format::smodels:
            out2 = std::make_unique<Potassco::SmodelsOutput>(os, potassco_, 0);
            out1 = std::make_unique<Potassco::SmodelsConvert>(*out2, potassco_);
            break;
        case Format::aspif_v1: out1 = std::make_unique<Potassco::AspifOutput>(os, 1); break;
        case Format::auto_   : [[fallthrough]];
        case Format::aspif   : out1 = std::make_unique<Potassco::AspifOutput>(os, 2); break;
    }
    in.peek() == 'a' ? Potassco::readAspif(in, *out1) : Potassco::readSmodels(in, *out1, opts);
}
catch (const std::exception& e) {
    handleException(e.what());
}

int main(int argc, char** argv) {
    LpConvert app;
    return app.main(argc, argv);
}
