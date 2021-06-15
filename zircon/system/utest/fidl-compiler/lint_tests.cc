// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

#define ASSERT_WARNINGS(quantity, lib, content)                  \
  do {                                                           \
    const auto& warnings = lib.lints();                          \
    if (strlen(content) != 0) {                                  \
      bool contains_content = false;                             \
      for (size_t i = 0; i < warnings.size(); i++) {             \
        if (warnings[i].find(content) != std::string::npos) {    \
          contains_content = true;                               \
          break;                                                 \
        }                                                        \
      }                                                          \
      ASSERT_TRUE(contains_content, content " not found");       \
    }                                                            \
    if (warnings.size() != quantity) {                           \
      std::string error = "Found warning: ";                     \
      for (size_t i = 0; i < warnings.size(); i++) {             \
        error.append(warnings[i]);                               \
      }                                                          \
      ASSERT_EQ(quantity, warnings.size(), "%s", error.c_str()); \
    }                                                            \
  } while (0)

TEST(LintTests, BadConstNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 bad_CONST = 1234;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_FALSE(converted.Lint());
  ASSERT_WARNINGS(1, converted, "bad_CONST");
}

TEST(LintTests, BadConstNamesKconst) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 kAllIsCalm = 1234;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_FALSE(converted.Lint());
  ASSERT_WARNINGS(1, converted, "kAllIsCalm");
  const auto& warnings = converted.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "ALL_IS_CALM");
}

TEST(LintTests, GoodConstNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 GOOD_CONST = 1234;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_TRUE(converted.Lint());
  ASSERT_WARNINGS(0, converted, "");
}

TEST(LintTests, BadProtocolNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

protocol URLLoader {};
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_FALSE(converted.Lint());
  ASSERT_WARNINGS(1, converted, "URLLoader");
  const auto& warnings = converted.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "UrlLoader");
}

TEST(LintTests, GoodProtocolNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

protocol UrlLoader {};
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_TRUE(converted.Lint());
  ASSERT_WARNINGS(0, converted, "");
}

TEST(LintTests, BadLibraryNamesBannedName) {
  TestLibrary library(R"FIDL(
library fuchsia.zxsocket;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_FALSE(converted.Lint());
  ASSERT_WARNINGS(1, converted, "zxsocket");
}

TEST(LintTests, BadUsingNames) {
  auto library = WithLibraryZx(R"FIDL(
library fuchsia.a;

using zx as bad_USING;

alias unused = bad_USING.handle;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_FALSE(converted.Lint());
  ASSERT_WARNINGS(1, converted, "bad_USING");
}

TEST(LintTests, GoodUsingNames) {
  auto library = WithLibraryZx(R"FIDL(
library fuchsia.a;

using zx as good_using;

alias unused = good_using.handle;
)FIDL");
  TestLibrary converted;
  ASSERT_COMPILED_AND_CONVERT_INTO(library, converted);
  ASSERT_TRUE(converted.Lint());
  ASSERT_WARNINGS(0, converted, "");
}

}  // namespace
