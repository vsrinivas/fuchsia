// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0)
    return 0;

  uint8_t lang_byte = *data;
  data++;
  size--;

  zxdb::ExprLanguage language;

  if (lang_byte & 1) {
    language = zxdb::ExprLanguage::kC;
  } else {
    language = zxdb::ExprLanguage::kRust;
  }

  std::string input(data, data + size);
  zxdb::ExprTokenizer tokenizer(input, language);

  tokenizer.Tokenize();

  if (tokenizer.err().has_error()) {
    return 0;
  }

  zxdb::ExprParser parser(tokenizer.tokens(), language);
  parser.ParseStandaloneExpression();

  return 0;
}
