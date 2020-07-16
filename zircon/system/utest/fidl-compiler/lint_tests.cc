// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

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

TEST(LintTest, const_names_bad) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 bad_CONST = 1234;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_CONST");
}

TEST(LintTest, const_names_kconst) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 kAllIsCalm = 1234;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "kAllIsCalm");
  const auto& warnings = library.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "ALL_IS_CALM");
}

TEST(LintTest, const_names_good) {
  TestLibrary library_yes(R"FIDL(
library fuchsia.a;

const uint64 GOOD_CONST = 1234;

)FIDL");
  ASSERT_TRUE(library_yes.Lint());
  ASSERT_WARNINGS(0, library_yes, "");
}

TEST(LintTest, protocol_names_bad) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

protocol URLLoader {};
)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "URLLoader");
  const auto& warnings = library.lints();
  ASSERT_SUBSTR(warnings[0].c_str(), "UrlLoader");
}

TEST(LintTest, protocol_names_good) {
  TestLibrary functioning(R"FIDL(
library fuchsia.a;

protocol UrlLoader {};
)FIDL");
  ASSERT_TRUE(functioning.Lint());
  ASSERT_WARNINGS(0, functioning, "");
}

TEST(LintTest, library_names_banned_name) {
  TestLibrary banned(R"FIDL(
library fuchsia.zxsocket;
)FIDL");
  ASSERT_FALSE(banned.Lint());
  ASSERT_WARNINGS(1, banned, "zxsocket");
}

TEST(LintTest, using_names_bad) {
  TestLibrary library(R"FIDL(
library fuchsia.a;

using foo as bad_USING;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_USING");
}

TEST(LintTest, using_names_good) {
  TestLibrary library_yes(R"FIDL(
library fuchsia.a;

using foo as good_using;
using bar as baz;

)FIDL");
  ASSERT_TRUE(library_yes.Lint());
  ASSERT_WARNINGS(0, library_yes, "");
}

}  // namespace
