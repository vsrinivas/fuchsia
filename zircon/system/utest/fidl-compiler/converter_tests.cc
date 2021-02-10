// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/converter.h>
#include <fidl/conversion.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

std::string Convert(const std::string& in, std::vector<std::string>& deps, fidl::ExperimentalFlags flags, fidl::conv::Conversion::Syntax syntax) {
  // Convert the test file, along with its deps, into a flat AST.
  TestLibrary flat_lib(in, flags);
  for (size_t i = 0; i < deps.size(); i++) {
    const std::string& dep = deps[i];
    flat_lib.AddSource("dep_lib_" + std::to_string(i) + ".file", dep);
  }
  flat_lib.Compile();

  // Read the file again, and convert it into a raw AST.
  TestLibrary raw_lib(in, flags);
  std::unique_ptr<fidl::raw::File> ast;
  raw_lib.Parse(&ast);

  // Run the ConvertingTreeVisitor using the two previously generated ASTs.
  fidl::conv::ConvertingTreeVisitor visitor = fidl::conv::ConvertingTreeVisitor(syntax, flat_lib.library());
  visitor.OnFile(ast);
  return *visitor.converted_output();
}

std::string ToOldSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::conv::Conversion::Syntax::kOld);
}

std::string ToOldSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::conv::Conversion::Syntax::kOld);
}

std::string ToNewSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::conv::Conversion::Syntax::kNew);
}

std::string ToNewSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::conv::Conversion::Syntax::kNew);
}

TEST(ConverterTests, AliasOfArray) {
  std::string old_version = R"FIDL(
library example;

alias foo = array<uint8>:5;
)FIDL";

  std::string new_version = R"FIDL(
library example;

alias foo = array<uint8,5>;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AliasOfHandleWithSubtype) {
  std::string old_version = R"FIDL(
library example;

alias foo = handle:VMO?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

alias foo = handle:<optional,VMO>;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AliasOfHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

alias foo = handle:<VMO,1>?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

alias foo = handle:<optional,VMO,1>;
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, AliasOfDeeplyNested) {
  std::string old_version = R"FIDL(
library example;

alias foo = vector<vector<array<uint8>:5>?>:9?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

alias foo = vector<vector<array<uint8,5>>:optional>:<optional,9>;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsUnmodified) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
bits Foo {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = bits {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsFlexible) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
flexible bits Foo {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = flexible bits {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsStrict) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
strict bits Foo {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict bits {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsUnmodifiedWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
bits Foo : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = bits : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsFlexibleWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
flexible bits Foo : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = flexible bits : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, BitsStrictWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
strict bits Foo : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict bits : uint64 {
  SMALLEST = 1;
  BIGGEST = 0x8000000000000000;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, Consts) {
  std::string old_version = R"FIDL(
library example;

const uint8 FOO = 34;
const string:3 BAR = "abc";
const bool BAZ = true;
)FIDL";

  std::string new_version = R"FIDL(
library example;

const FOO uint8 = 34;
const BAR string:3 = "abc";
const BAZ bool = true;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumUnmodified) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
enum Foo {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = enum {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumFlexible) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
flexible enum Foo {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = flexible enum {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumStrict) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
strict enum Foo {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict enum {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumUnmodifiedWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
enum Foo : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = enum : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumFlexibleWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
flexible enum Foo : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = flexible enum : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, EnumStrictWithWrappedType) {
  std::string old_version = R"FIDL(
library example;

/// Doc comment.
strict enum Foo : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict enum : uint64 {
  FOO = 1;
  BAR = 2;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, Protocol) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b);
}
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(a string, b int32);
}
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithResponse) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b) -> (bool c);
}
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(a string, b int32) -> (c bool);
}
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithResponseAndError) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b) -> (bool c) error int32;
}
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(a string, b int32) -> (c bool) error int32;
}
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
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

TEST(ConverterTests, TableEmpty) {
  std::string old_version = R"FIDL(
library example;

table T {};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type T = table {};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithMember) {
  std::string old_version = R"FIDL(
library example;

table T {
  4: int32 a;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type T = table {
  4: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithVectors) {
  std::string old_version = R"FIDL(
library example;

table T {
  1: vector<uint8> v1;
  2: vector<array<uint8>:4>:16 v2;
  3: vector<vector<array<uint8>:4>:16?>:32 v3;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type T = table {
  1: v1 vector<uint8>;
  2: v2 vector<array<uint8,4>>:16;
  3: v3 vector<vector<array<uint8,4>>:<optional,16>>:32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithHandleWithSubtype) {
  std::string old_version = R"FIDL(
library example;

resource table T {
  1: handle:VMO h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type T = resource table {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

resource table T {
  1: handle:<CHANNEL,7> h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type T = resource table {
  1: h handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, TableWithComments) {
  std::string old_version = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
table T {
  // Inner comments should be retained.
  /// So should inner doc comments.
  1: string a;

  /// Doc comment reserved.
  // Comment reserved.
  2: reserved;

  // And leading blank lines.
  // And multiline comments.
  3: int32 b;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
type T = table {
  // Inner comments should be retained.
  /// So should inner doc comments.
  1: a string;

  /// Doc comment reserved.
  // Comment reserved.
  2: reserved;

  // And leading blank lines.
  // And multiline comments.
  3: b int32;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberUnmodified) {
  std::string old_version = R"FIDL(
library example;

union U {
  1: int32 a;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberFlexible) {
  std::string old_version = R"FIDL(
library example;

flexible union U {
  1: int32 a;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = flexible union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberStrict) {
  std::string old_version = R"FIDL(
library example;

strict union U {
  1: int32 a;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = strict union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithVectors) {
  std::string old_version = R"FIDL(
library example;

union U {
  1: vector<uint8> v1;
  2: vector<array<uint8>:4>:16 v2;
  3: vector<vector<array<uint8>:4>:16?>:32 v3;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = union {
  1: v1 vector<uint8>;
  2: v2 vector<array<uint8,4>>:16;
  3: v3 vector<vector<array<uint8,4>>:<optional,16>>:32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeUnmodified) {
  std::string old_version = R"FIDL(
library example;

resource union U {
  1: handle:VMO h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = resource union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeFlexible) {
  std::string old_version = R"FIDL(
library example;

resource flexible union U {
  1: handle:VMO h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = resource flexible union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeStrict) {
  std::string old_version = R"FIDL(
library example;

resource strict union U {
  1: handle:VMO h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = resource strict union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

resource union U {
  1: handle:<CHANNEL,7> h;
};
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

type U = resource union {
  1: h handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, UnionWithComments) {
  std::string old_version = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
union U {
  // Inner comments should be retained.
  /// So should inner doc comments.
  1: string a;


  2: reserved;

  // And leading blank lines.
  // And multiline comments.
  3: int32 b;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  std::string ftp50 = R"FIDL(
library example;

// Top-level comments should be retained.
/// Top-level doc comments should be retained.
// Top-level comments after doc comments should be retained.
type U = union {
  // Inner comments should be retained.
  /// So should inner doc comments.
  1: a string;


  2: reserved;

  // And leading blank lines.
  // And multiline comments.
  3: b int32;
  // Trailing inner comments should be retained.
};
// Trailing comments should be retained.
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(ftp50, ToNewSyntax(old_version));
}

TEST(ConverterTests, Unchanged) {
  std::string old_version = R"FIDL(
library example;

// Comment.
/// Doc Comment.
// Another Comment.
using foo;

/// Doc Comment.
[Transport = "Syscall"]
protocol Empty {};

service AlsoEmpty {};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(old_version, ToNewSyntax(old_version));
}

}  // namespace

