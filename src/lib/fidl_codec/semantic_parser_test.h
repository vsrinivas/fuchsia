// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_TEST_H_
#define SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_TEST_H_

#include "gtest/gtest.h"
#include "library_loader.h"

namespace fidl_codec {
namespace semantic {

class SemanticParserTest : public ::testing::Test {
 public:
  SemanticParserTest();

 protected:
  void SetUp() override;

  LibraryReadError err_;
  LibraryLoader library_loader_;
};

}  // namespace semantic
}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_TEST_H_
