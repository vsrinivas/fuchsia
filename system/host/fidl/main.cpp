// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lib/identifier_table.h"
#include "lib/lexer.h"
#include "lib/parser.h"
#include "lib/source_manager.h"

namespace fidl {
namespace {

enum struct Behavior {
    None,
};

bool TestParser(int file_count, char** file_names, Behavior behavior) {
    SourceManager source_manager;
    ErrorReporter error_reporter;
    IdentifierTable identifier_table;

    for (int idx = 0; idx < file_count; ++idx) {
        const SourceFile* source_file = source_manager.CreateSource(file_names[idx]);
        if (source_file == nullptr) {
            fprintf(stderr, "Couldn't read in source data from %s\n", file_names[idx]);
            return false;
        }

        Lexer lexer(*source_file, &identifier_table);
        Parser parser(&lexer, &error_reporter);

        auto raw_ast = parser.Parse();
        if (!parser.Ok()) {
            error_reporter.PrintReports();
            return false;
        }
    }

    return true;
}

} // namespace
} // namespace fidl

int main(int argc, char* argv[]) {
    if (argc < 3)
        return 1;

    // Parse the program name.
    --argc;
    ++argv;

    // Parse the behavior.
    fidl::Behavior behavior;
    if (!strncmp(argv[0], "none", 4))
        behavior = fidl::Behavior::None;
    else
        return 1;
    --argc;
    ++argv;

    return TestParser(argc, argv, behavior) ? 0 : 1;
}
