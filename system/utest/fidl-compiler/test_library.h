// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>


static fidl::SourceFile MakeSourceFile(const std::string& filename, const std::string& raw_source_code) {
    std::string source_code(raw_source_code);
    // NUL terminate the string.
    source_code.resize(source_code.size() + 1);
    return fidl::SourceFile(filename, source_code);
}

class TestLibrary {
public:
    TestLibrary(const std::string& filename, const std::string& raw_source_code)
        : source_file_(MakeSourceFile(filename, raw_source_code)),
          lexer_(source_file_, &identifier_table_),
          parser_(&lexer_, &error_reporter_),
          library_(&compiled_libraries_, &error_reporter_) {
    }

    bool Parse() {
        auto ast = parser_.Parse();
        return parser_.Ok() &&
               library_.ConsumeFile(std::move(ast)) &&
               library_.Compile();
    }

    const fidl::flat::Struct* LookupStruct(const std::string& name) {
        for (const auto& struct_decl : library_.struct_declarations_) {
            if (struct_decl->GetName() == name) {
                return struct_decl.get();
            }
        }
        return nullptr;
    }

    const fidl::flat::Union* LookupUnion(const std::string& name) {
        for (const auto& union_decl : library_.union_declarations_) {
            if (union_decl->GetName() == name) {
                return union_decl.get();
            }
        }
        return nullptr;
    }

private:
    fidl::SourceFile source_file_;
    fidl::IdentifierTable identifier_table_;
    fidl::ErrorReporter error_reporter_;
    fidl::Lexer lexer_;
    fidl::Parser parser_;
    std::map<std::vector<fidl::StringView>, std::unique_ptr<fidl::flat::Library>> compiled_libraries_;
    fidl::flat::Library library_;
};



#endif // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_
