// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fidl/lexer.h>
#include <fidl/linter.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>
#include <fidl/tree_visitor.h>

namespace {

void Usage(const std::string& argv0) {
    std::cout
        << "usage: " << argv0 << " <options> <files>\n"
                                 " * `-h, --help`                   Prints this help, and exits immediately.\n"
                                 "\n";
    std::cout.flush();
}

[[noreturn]] void FailWithUsage(
    const std::string& argv0, const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    Usage(argv0);
    exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    exit(1);
}

bool Lint(const fidl::SourceFile& source_file,
          fidl::ErrorReporter* error_reporter, std::string& output) {
    fidl::Lexer lexer(source_file, error_reporter);
    fidl::Parser parser(&lexer, error_reporter);
    std::unique_ptr<fidl::raw::File> ast = parser.Parse();
    if (!parser.Ok()) {
        return false;
    }
    fidl::Findings findings;
    fidl::linter::Linter linter;
    if (linter.Lint(ast, &findings)) {
        return true;
    }
    fidl::utils::WriteFindingsToErrorReporter(findings, error_reporter);
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    // Construct the args vector from the argv array.
    std::vector<std::string> args(argv, argv + argc);
    size_t pos = 1;
    // Process options
    while (pos < args.size() && args[pos] != "--" && args[pos].find("-") == 0) {
        if (args[pos] == "-h" || args[pos] == "--help") {
            Usage(args[0]);
            exit(0);
        } else {
            FailWithUsage(args[0], "Unknown argument: %s\n", args[pos].c_str());
        }
        pos++;
    }

    if (pos >= args.size()) {
        // TODO: Should probably read a file from stdin, instead.
        FailWithUsage(args[0], "No files provided\n");
    }

    fidl::SourceManager source_manager;

    // Process filenames.
    for (size_t i = pos; i < args.size(); i++) {
        if (!source_manager.CreateSource(args[i])) {
            Fail("Couldn't read in source data from %s\n", args[i].c_str());
        }
    }

    fidl::ErrorReporter error_reporter;
    for (const auto& source_file : source_manager.sources()) {
        std::string output;
        if (!Lint(*source_file, &error_reporter, output)) {
            // In the formattter, we do not print the report if there are only
            // warnings.
            error_reporter.PrintReports();
            return 1;
        }
    }
}
