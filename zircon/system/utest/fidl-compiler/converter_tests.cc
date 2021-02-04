// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/converter.h>
#include <fidl/conversion.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

std::string Convert(const std::string& in, fidl::ExperimentalFlags flags, fidl::conv::Conversion::Syntax syntax) {
  TestLibrary library(in, flags);
  std::unique_ptr<fidl::raw::File> ast;
  library.Parse(&ast);
  fidl::conv::ConvertingTreeVisitor visitor = fidl::conv::ConvertingTreeVisitor(syntax);
  visitor.OnFile(ast);
  return *visitor.converted_output();
}

std::string ToOldSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  return Convert(in, flags, fidl::conv::Conversion::Syntax::kOld);
}

std::string ToOldSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  return Convert(in, flags, fidl::conv::Conversion::Syntax::kOld);
}

std::string ToNewSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  return Convert(in, flags, fidl::conv::Conversion::Syntax::kNew);
}

std::string ToNewSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  return Convert(in, flags, fidl::conv::Conversion::Syntax::kNew);
}

TEST(ConverterTests, StructEmpty) {
  std::string old_version = R"FIDL(
library example;

struct S {};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = struct {};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithMember) {
  std::string old_version = R"FIDL(
library example;

struct S {
  int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = struct {
  a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithOptional) {
  std::string old_version = R"FIDL(
library example;

struct O {};

struct S {
  O? o;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type O = struct {};

type S = struct {
  o O:optional;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithVectors) {
  std::string old_version = R"FIDL(
library example;

struct S {
  vector<uint8> v1;
  vector<uint8>? v2;
  vector<uint8>:16? v3;
  vector<vector<uint8>?>:16 v4;
  vector<vector<vector<uint8>:16?>>? v5;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = struct {
  v1 vector<uint8>;
  v2 vector<uint8>:optional;
  v3 vector<uint8>:<optional,16>;
  v4 vector<vector<uint8>:optional>:16;
  v5 vector<vector<vector<uint8>:<optional,16>>>:optional;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithHandle) {
  std::string old_version = R"FIDL(
library example;

resource struct S {
  handle? h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = resource struct {
  h handle:optional;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithHandleWithSubtype) {
  std::string old_version = R"FIDL(
library example;

resource struct S {
  handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = resource struct {
  h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

resource struct S {
  handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = resource struct {
  h handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

// This test case's purpose to verify that "nested conversions" work well.  This
// particular case has four levels of nesting: the struct declaration at the top
// level, which contains an identifier/type order swap conversion, which
// contains an array type declaration, which itself contains a handle type
// declaration that needs to be converted as well.
TEST(ConverterTests, StructWithManyNestedConversions) {
  std::string old_version = R"FIDL(
library example;

resource struct S {
  array<handle:<PORT,7>?>:5 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = resource struct {
  a array<handle:<optional,PORT,7>,5>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, StructWithComments) {
  std::string old_version = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
struct S {
  // Inner comments should be retained.
  /// So should inner doc comments.
  string a;

  // And leading blank lines.
  // And multiline comments.
  int32 b;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  std::string new_version = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
type S = struct {
  // Inner comments should be retained.
  /// So should inner doc comments.
  a string;

  // And leading blank lines.
  // And multiline comments.
  b int32;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

}  // namespace
