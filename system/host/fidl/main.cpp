// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "identifier_table.h"
#include "lexer.h"
#include "make_unique.h"
#include "parser.h"

namespace fidl {
namespace {

bool MakeSourceData(const char* filename, std::string* out_source) {
    FILE* file = fopen(filename, "rb");
    if (!file)
        return false;

    // The lexer requires zero terminated data.
    std::string source;
    fseek(file, 0, SEEK_END);
    auto filesize = ftell(file);
    source.reserve(filesize + 1);
    rewind(file);
    fread(&source[0], 1, filesize, file);
    source[filesize] = 0;
    fclose(file);

    *out_source = std::move(source);
    return true;
}

enum struct Behavior {
    None,
};

bool TestParser(std::string source, Behavior behavior) {
    IdentifierTable identifier_table;
    Lexer lexer(source, &identifier_table);
    Parser parser(&lexer);

    auto raw_ast = parser.Parse();
    if (!parser.Ok()) {
        fprintf(stderr, "Parse failed!\n");
        return false;
    }

    return true;
}

} // namespace
} // namespace fidl

int main(int argc, char* argv[]) {
    if (argc != 3)
        return 1;

    fidl::Behavior behavior;
    if (!strncmp(argv[1], "none", 4))
        behavior = fidl::Behavior::None;
    else
        return 1;

    std::string source;
    if (!fidl::MakeSourceData(argv[2], &source)) {
        fprintf(stderr, "Couldn't read in source data from %s\n", argv[2]);
        return 1;
    }

    return TestParser(std::move(source), behavior) ? 0 : 1;
}
