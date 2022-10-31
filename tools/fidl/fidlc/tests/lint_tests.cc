// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

#define ASSERT_WARNINGS(quantity, lib, content)                  \
  do {                                                           \
    const auto& warnings = (lib).lints();                        \
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
    if (warnings.size() != (quantity)) {                         \
      std::string error = "Found warning: ";                     \
      for (size_t i = 0; i < warnings.size(); i++) {             \
        error.append(warnings[i]);                               \
      }                                                          \
      ASSERT_EQ(quantity, warnings.size(), "%s", error.c_str()); \
    }                                                            \
  } while (0)

TEST(LintTests, BadConstNames) {
  TestLibrary library(R"FIDL(library fuchsia.a;

const bad_CONST uint64 = 1234;
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_CONST");
}

TEST(LintTests, BadConstNamesKconst) {
  TestLibrary library(R"FIDL(library fuchsia.a;

const kAllIsCalm uint64 = 1234;
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "kAllIsCalm");
  const auto& warnings = library.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "ALL_IS_CALM");
}

TEST(LintTests, GoodConstNames) {
  TestLibrary library(R"FIDL(library fuchsia.a;

const GOOD_CONST uint64 = 1234;
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_TRUE(library.Lint());
  ASSERT_WARNINGS(0, library, "");
}

TEST(LintTests, BadProtocolNames) {
  TestLibrary library(R"FIDL(library fuchsia.a;

protocol URLLoader {};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "URLLoader");
  const auto& warnings = library.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "UrlLoader");
}

TEST(LintTests, GoodProtocolNames) {
  TestLibrary library(R"FIDL(library fuchsia.a;

protocol UrlLoader {};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_TRUE(library.Lint());
  ASSERT_WARNINGS(0, library, "");
}

TEST(LintTests, BadLibraryNamesBannedName) {
  TestLibrary library(R"FIDL(library fuchsia.zxsocket;
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "zxsocket");
}

TEST(LintTests, BadUsingNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

using zx as bad_USING;

alias Unused = bad_USING.handle;
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_USING");
}

TEST(LintTests, GoodUsingNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

using zx as good_using;

alias Unused = good_using.handle;
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  ASSERT_TRUE(library.Lint());
  ASSERT_WARNINGS(0, library, "");
}

TEST(LintTests, BadAliasNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

alias snake_case = uint32;
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "snake_case");
}

TEST(LintTests, GoodAliasNames) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

alias SnakeCase = uint32;
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  ASSERT_TRUE(library.Lint());
  ASSERT_WARNINGS(0, library, "");
}

// TODO(fxbug.dev/7807): Delete this test once new-types are supported.
// This is a case where compilation would fail, but since the linter only operates on the parsed
// raw AST, we would not yet know it. Thus, we expect compilation to fail, but linting to pass.
TEST(LintTests, GoodIgnoreNewTypes) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

type TransactionId = uint64;
)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_TRUE(library.Lint());
}

}  // namespace
