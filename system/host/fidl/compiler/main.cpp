// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fidl/c_generator.h>
#include <fidl/flat_ast.h>
#include <fidl/identifier_table.h>
#include <fidl/json_generator.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>

namespace {

[[noreturn]] void Usage() {
    std::cout << "fidl usage:\n"
                 "    fidl [--c-header HEADER_PATH]\n"
                 "         [--json JSON_PATH]\n"
                 "         --files [FIDL_FILE...]\n"
                 "    * If no output types are provided, the FIDL_FILES are parsed and\n"
                 "      compiled, but no output is produced. Otherwise:\n"
                 "    * If --c-header is provided, C structures are generated\n"
                 "        into HEADER_PATH.\n"
                 "    * If --json is provided, JSON intermediate data is generated\n"
                 "        into JSON_PATH.\n"
                 "    The --file [FIDL_FILE...] arguments can also be provided via a\n"
                 "    response file, denoted as `@filepath'. The contents of the file at\n"
                 "    `filepath' will be interpreted as a whitespace-delimited list\n"
                 "    of files to parse. Response files cannot be nested, and must be.\n"
                 "    the last argument.\n";
    std::cout.flush();
    exit(1);
}

std::fstream Open(std::string filename, std::ios::openmode mode) {
    std::fstream stream;
    stream.open(filename, mode);
    if (!stream.is_open()) {
        Usage();
    }
    return stream;
}

class Arguments {
public:
    virtual ~Arguments() {}

    virtual std::string Claim() = 0;
    virtual bool Remaining() const = 0;
};

class ArgvArguments : public Arguments {
public:
    ArgvArguments(int count, char** arguments)
        : count_(count), arguments_(const_cast<const char**>(arguments)) {}

    std::string Claim() override {
        if (count_ < 1) {
            Usage();
        }
        std::string argument = arguments_[0];
        --count_;
        ++arguments_;
        return argument;
    }

    bool Remaining() const override { return count_ > 0; }

    bool HeadIsResponseFile() {
        if (count_ != 1) {
            return false;
        }
        return arguments_[0][0] == '@';
    }

private:
    int count_;
    const char** arguments_;
};

class ResponseFileArguments : public Arguments {
public:
    ResponseFileArguments(fidl::StringView filename)
        : file_(Open(filename, std::ios::in)) {
        ConsumeWhitespace();
    }

    std::string Claim() override {
        std::string argument;
        while (Remaining() && !IsWhitespace()) {
            argument.push_back(file_.get());
        }
        ConsumeWhitespace();
        return argument;
    }

    bool Remaining() const override {
        return !file_.eof();
    }

private:
    bool IsWhitespace() {
        switch (file_.peek()) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            return true;
        default:
            return false;
        }
    }

    void ConsumeWhitespace() {
        while (Remaining() && IsWhitespace()) {
            file_.get();
        }
    }

    std::fstream file_;
};

enum struct Behavior {
    CHeader,
    JSON,
};

bool Parse(const fidl::SourceFile& source_file, fidl::IdentifierTable* identifier_table,
           fidl::ErrorReporter* error_reporter, fidl::flat::Library* library) {
    fidl::Lexer lexer(source_file, identifier_table);
    fidl::Parser parser(&lexer, error_reporter);
    auto ast = parser.Parse();
    if (!parser.Ok()) {
        error_reporter->PrintReports();
        return false;
    }

    if (!library->ConsumeFile(std::move(ast))) {
        fprintf(stderr, "Parse failed!\n");
        return false;
    }

    return true;
}

template <typename Generator>
void Generate(Generator* generator, std::fstream file) {
    auto generated_output = generator->Produce();
    file << generated_output.str();
    file.flush();
}

} // namespace

int main(int argc, char* argv[]) {
    auto argv_args = std::make_unique<ArgvArguments>(argc, argv);

    // Parse the program name.
    argv_args->Claim();

    // Parse output types.
    std::unique_ptr<ResponseFileArguments> response_file_args;
    Arguments* args = argv_args.get();
    std::map<Behavior, std::fstream> outputs;
    while (argv_args->Remaining()) {
        // Do we have a response file? If so, switch to parsing it.
        if (argv_args->HeadIsResponseFile()) {
            if (response_file_args != nullptr) {
                // Disallow nested response files.
                Usage();
            }
            std::string response = args->Claim();
            if (argv_args->Remaining()) {
                // Response file must be the last argument.
                Usage();
            }
            // Drop the leading '@'.
            fidl::StringView response_file = response.data() + 1;
            response_file_args = std::make_unique<ResponseFileArguments>(response_file);
            args = response_file_args.get();
            // Start parsing filenames.
            break;
        }

        // Try to parse an output type.
        std::string behavior_argument = argv_args->Claim();
        std::fstream output_file;
        if (behavior_argument == "--c-header") {
            outputs.emplace(Behavior::CHeader, Open(argv_args->Claim(), std::ios::out));
        } else if (behavior_argument == "--json") {
            outputs.emplace(Behavior::JSON, Open(argv_args->Claim(), std::ios::out));
        } else if (behavior_argument == "--files") {
            // Start parsing filenames.
            break;
        } else {
            Usage();
        }
    }

    // Parse source files.
    fidl::SourceManager source_manager;
    while (args->Remaining()) {
        std::string filename = args->Claim();
        const fidl::SourceFile* source = source_manager.CreateSource(filename.data());
        if (source == nullptr) {
            fprintf(stderr, "Couldn't read in source data from %s\n", filename.data());
            return 1;
        }
    }

    fidl::IdentifierTable identifier_table;
    fidl::ErrorReporter error_reporter;
    fidl::flat::Library library;
    for (const auto& source_file : source_manager.sources()) {
        if (!Parse(source_file, &identifier_table, &error_reporter, &library)) {
            return 1;
        }
    }
    if (!library.Resolve()) {
        fprintf(stderr, "flat::Library resolution failed!\n");
        return 1;
    }

    for (auto& output : outputs) {
        auto& behavior = output.first;
        auto& output_file = output.second;

        switch (behavior) {
        case Behavior::CHeader: {
            fidl::CGenerator generator(&library);
            Generate(&generator, std::move(output_file));
            break;
        }
        case Behavior::JSON: {
            fidl::JSONGenerator generator(&library);
            Generate(&generator, std::move(output_file));
            break;
        }
        }
    }
}
