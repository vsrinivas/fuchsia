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

#include "lib/c_generator.h"
#include "lib/json_generator.h"
#include "lib/identifier_table.h"
#include "lib/lexer.h"
#include "lib/library.h"
#include "lib/parser.h"
#include "lib/source_manager.h"

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
    std::cout.flush();
    exit(1);
}

class Arguments {
public:
    Arguments(int count, char** arguments)
        : count_(count), arguments_(const_cast<const char**>(arguments)) {}

    std::string Claim() {
        if (count_ < 1) {
            Usage();
        }
        std::string argument = arguments_[0];
        --count_;
        ++arguments_;
        return argument;
    }

    bool Remaining() { return count_ > 0; }

private:
    int count_;
    const char** arguments_;
};

std::fstream Open(std::string filename) {
    std::fstream stream;
    stream.open(filename, std::ios::out);
    return stream;
}

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
    Arguments args(argc, argv);
    // Parse the program name.
    args.Claim();

    std::string behavior_argument = args.Claim();
    Behavior behavior;
    std::fstream output_file;
    if (behavior_argument == "c-structs") {
        behavior = Behavior::CStructs;
        // Parse a file name to write output to.
        if (argc < 2) {
            return 1;
        }
        output_file = Open(args.Claim());
    } else if (behavior_argument == "json") {
        behavior = Behavior::JSON;
        // Parse a file name to write output to.
        if (argc < 2) {
            return 1;
        }
        output_file = Open(args.Claim());
    } else {
        Usage();
    }

    fidl::SourceManager source_manager;
    fidl::IdentifierTable identifier_table;
    fidl::ErrorReporter error_reporter;
    fidl::Library library;
    if (!Parse(&args, &source_manager, &identifier_table, &error_reporter, &library)) {
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
