// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lexer.h"
#include "parser.h"
#include "test.h"


bool TestParserStringInput(std::string string_input) {
  // the lexer needs the last character to be zero
  std::string extended_input = string_input.append(1, 0);
  f1dl::StringView stringview_input = f1dl::StringView(extended_input);

  f1dl::IdentifierTable identifier_table;
  f1dl::Lexer lexer(stringview_input, &identifier_table);
  f1dl::Parser parser(&lexer);

  auto raw_ast = parser.Parse();
  if (!parser.Ok()) {
    fprintf(stderr, "Parse failed!\n");
    return false;
  }

  return true;
}


bool TestParserByteArrayInput(const uint8_t* byte_data, size_t size) {
  std::string string_data(reinterpret_cast<const char*> (byte_data), size);
  return TestParserStringInput(std::move(string_data));
}
