// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/error_reporter.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string string_data(reinterpret_cast<const char*>(data), size);
  // The lexer needs the last character to be zero.
  string_data.append(1, 0);

  fidl::SourceManager source_manager;
  fidl::IdentifierTable identifier_table;
  fidl::ErrorReporter error_reporter;
  const fidl::SourceFile* source = source_manager.CreateSource(string_data);
  fidl::Lexer lexer(*source, &identifier_table);
  fidl::Parser parser(&lexer, &error_reporter);

  auto raw_ast = parser.Parse();
  if (!parser.Ok())
    return 1;

  return 0;
}
