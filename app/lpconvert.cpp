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

#include <potassco/program_opts/typed_value.h>

#include <cctype>
#include <fstream>
#include <iostream>

using namespace Potassco::ProgramOptions;

class LpConvert : public Potassco::Application {
public:
    [[nodiscard]] const char* getName() const override { return "lpconvert"; }
    [[nodiscard]] const char* getVersion() const override { return "2.0.0-banane"; }
    [[nodiscard]] const char* getPositional(const std::string&) const override { return "input"; }
    [[nodiscard]] const char* getUsage() const override {
        return "[options] [<file>]\n"
               "Convert program in <file> or standard input";
    }
    void initOptions(OptionContext& root) override;
    void validateOptions(const OptionContext&, const ParsedOptions&, const ParsedValues&) override {}
    void setup() override {}
    void run() override;
    void printVersion(std::ostream& os) override {
        Potassco::Application::printVersion(os);
        os << "libpotassco version " << LIB_POTASSCO_VERSION
           << "\nCopyright (C) Benjamin Kaufmann\n"
              "License: The MIT License <https://opensource.org/licenses/MIT>\n";
    }

private:
    std::string input_;
    std::string output_;
    bool        potassco_ = false;
    bool        filter_   = false;
    bool        text_     = false;
};

void LpConvert::initOptions(OptionContext& root) {
    OptionGroup convert("Conversion Options");
    convert.addOptions()                                                                                         //
        ("input,i,@2", storeTo(input_, std::string()), "Input file")                                             //
        ("potassco,p", flag(potassco_, false), "Enable potassco extensions")                                     //
        ("filter,f", flag(filter_, false), "Hide converted potassco predicates")                                 //
        ("output,o", storeTo(output_, std::string())->arg("<file>"), "Write output to <file> (default: stdout)") //
        ("text,t", flag(text_, false), "Convert to ground text format")                                          //
        ;
    root.add(convert);
}
void LpConvert::run() {
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
    std::istream&             in = iFile.is_open() ? iFile : std::cin;
    std::ostream&             os = oFile.is_open() ? oFile : std::cout;
    Potassco::AspifTextOutput text(os);
    POTASSCO_CHECK(in.peek() == 'a' || std::isdigit(in.peek()), std::errc::not_supported,
                   "Unrecognized input format '%c' - expected 'aspif' or <digit>", in.peek());
    if (in.peek() == 'a') {
        Potassco::SmodelsOutput  writer(os, potassco_, 0);
        Potassco::SmodelsConvert smodels(writer, potassco_);
        Potassco::readAspif(in, not text_ ? static_cast<Potassco::AbstractProgram&>(smodels) : text);
    }
    else {
        Potassco::AspifOutput           aspif(os);
        Potassco::SmodelsInput::Options opts;
        if (potassco_) {
            opts.enableClaspExt().convertEdges().convertHeuristic();
            if (filter_) {
                opts.dropConverted();
            }
        }
        Potassco::readSmodels(in, not text_ ? static_cast<Potassco::AbstractProgram&>(aspif) : text, opts);
    }
}

int main(int argc, char** argv) {
    LpConvert app;
    return app.main(argc, argv);
}
