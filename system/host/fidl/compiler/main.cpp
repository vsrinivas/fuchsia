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
#include <fidl/identifier_table.h>
#include <fidl/json_generator.h>
#include <fidl/lexer.h>
#include <fidl/library.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>

namespace {

[[noreturn]] void Usage() {
    std::cout << "fidl usage:\n";
    std::cout << "    fidl c-structs HEADER_PATH [FIDL_FILE...]\n";
    std::cout << "        Parses the FIDL_FILEs and generates C structures\n";
    std::cout << "        into HEADER_PATH.\n";
    std::cout << "\n";
    std::cout << "    fidl json JSON_PATH [FIDL_FILE...]\n";
    std::cout << "        Parses the FIDL_FILEs and generates JSON intermediate data\n";
    std::cout << "        into JSON_PATH.\n";
    std::cout << "\n";
    std::cout << "The [FIDL_FILE...] arguments can also be provided via a response\n";
    std::cout << "    file, denoted as `@filepath'. The contents of the file at\n";
    std::cout << "    `filepath' will be interpreted as a whitespace-delimited list\n";
    std::cout << "    of files to parse.\n";
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
    ResponseFileArguments(fidl::StringView filename) : file_(Open(filename, std::ios::in)) {
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
    CStructs,
    JSON,
};

bool Parse(Arguments* args, fidl::SourceManager* source_manager,
           fidl::IdentifierTable* identifier_table, fidl::ErrorReporter* error_reporter,
           fidl::Library* library) {
    while (args->Remaining()) {
        std::string filename = args->Claim();
        const fidl::SourceFile* source = source_manager->CreateSource(filename.data());
        if (source == nullptr) {
            fprintf(stderr, "Couldn't read in source data from %s\n", filename.data());
            return false;
        }

        fidl::Lexer lexer(*source, identifier_table);
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
    }

    if (!library->Resolve()) {
        fprintf(stderr, "Library resolution failed!\n");
        return false;
    }

    return true;
}

bool GenerateC(fidl::Library* library, std::fstream header_output) {
    std::ostringstream header_file;
    fidl::CGenerator c_generator(library);

    c_generator.ProduceCStructs(&header_file);

    header_output << "// header file\n";
    header_output << header_file.str();
    header_output.flush();

    return true;
}

bool GenerateJSON(fidl::Library* library, std::fstream json_output) {
    std::ostringstream json_file;
    fidl::JSONGenerator json_generator(library);

    json_generator.ProduceJSON(&json_file);

    json_output << json_file.str();
    json_output.flush();

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    auto argv_args = std::make_unique<ArgvArguments>(argc, argv);

    // Parse the program name.
    argv_args->Claim();

    std::string behavior_argument = argv_args->Claim();
    Behavior behavior;
    std::fstream output_file;
    if (behavior_argument == "c-structs") {
        behavior = Behavior::CStructs;
        // Parse a file name to write output to.
        if (argc < 2) {
            return 1;
        }
        output_file = Open(argv_args->Claim(), std::ios::out);
    } else if (behavior_argument == "json") {
        behavior = Behavior::JSON;
        // Parse a file name to write output to.
        if (argc < 2) {
            return 1;
        }
        output_file = Open(argv_args->Claim(), std::ios::out);
    } else {
        Usage();
    }

    // Either continue with the list of fidl files to compile, or else
    // with a response file.
    std::unique_ptr<ResponseFileArguments> response_file_args;
    Arguments* args = argv_args.get();
    if (argv_args->HeadIsResponseFile()) {
        std::string response = argv_args->Claim();
        // Drop the leading '@'.
        fidl::StringView response_file = response.data() + 1;
        response_file_args = std::make_unique<ResponseFileArguments>(response_file);
        args = response_file_args.get();
    }

    fidl::SourceManager source_manager;
    fidl::IdentifierTable identifier_table;
    fidl::ErrorReporter error_reporter;
    fidl::Library library;
    if (!Parse(args, &source_manager, &identifier_table, &error_reporter, &library)) {
        return 1;
    }

    switch (behavior) {
    case Behavior::CStructs: {
        if (!GenerateC(&library, std::move(output_file))) {
            return 1;
        }
        break;
    }
    case Behavior::JSON: {
        if (!GenerateJSON(&library, std::move(output_file))) {
            return 1;
        }
        break;
    }
    }
}
