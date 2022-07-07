// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/index_json_generator.h"

#include <zircon/assert.h>

#include "fidl/flat/name.h"
#include "fidl/flat/types.h"
#include "fidl/flat_ast.h"
#include "fidl/names.h"
#include "fidl/types.h"

namespace fidl {

std::ostringstream IndexJSONGenerator::Produce() {
  ResetIndentLevel();
  GenerateObject([&]() {
    GenerateObjectMember("name", flat::LibraryName(compilation_->library_name, "."),
                         Position::kFirst);
    GenerateObjectMember("lib_declarations", compilation_->library_declarations);
    GenerateObjectMember("using_declarations", compilation_->using_references);
    GenerateObjectMember("dependencies", compilation_->direct_and_composed_dependencies);
  });
  GenerateEOF();

  return std::move(json_file_);
}
void IndexJSONGenerator::Generate(SourceSpan value) {
  GenerateObject([&]() {
    // for debugging purpose, include the span data
    GenerateObjectMember("data", value.data(), Position::kFirst);
    auto start_offset = value.data().data() - value.source_file().data().data();
    GenerateObjectMember("start_offset", start_offset);
    GenerateObjectMember("end_offset", start_offset + value.data().size());
    GenerateObjectMember("file", value.source_file().filename());
  });
}

void IndexJSONGenerator::Generate(std::pair<flat::Library*, SourceSpan> reference) {
  GenerateObject([&]() {
    // for debugging purpose, include the span data
    GenerateObjectMember("library_name", flat::LibraryName(reference.first->name, "."),
                         Position::kFirst);
    GenerateObjectMember("referenced_at", reference.second);
  });
}

void IndexJSONGenerator::Generate(const flat::Compilation::Dependency& dependency) {
  GenerateObject([&]() {
    GenerateObjectMember("library_name", flat::LibraryName(dependency.library->name, "."),
                         Position::kFirst);
    GenerateObjectMember("library_location", dependency.library->arbitrary_name_span);
  });
}

}  // namespace fidl
