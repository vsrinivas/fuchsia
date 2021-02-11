// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/converter.h>
#include <fidl/conversion.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace {
std::string Convert(const std::string& in, const std::vector<std::string>& deps, fidl::ExperimentalFlags flags, fidl::conv::Conversion::Syntax syntax) {
  // Convert the test file, along with its deps, into a flat AST.
  SharedAmongstLibraries shared;
  TestLibrary flat_lib("example.fidl", in, &shared, flags);
  for (size_t i = 0; i < deps.size(); i++) {
    TestLibrary dependency("dep" + std::to_string(i + 1) + ".fidl", deps[i], &shared, flags);
    dependency.Compile();
    flat_lib.AddDependentLibrary(std::move(dependency));
  }
  flat_lib.Compile();

  // Read the file again, and convert it into a raw AST.
  TestLibrary raw_lib("example.fidl", in, flags);
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

std::string ToOldSyntax(const std::string& in, const std::vector<std::string>& deps, fidl::ExperimentalFlags flags) {
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

std::string ToNewSyntax(const std::string& in, const std::vector<std::string>& deps, fidl::ExperimentalFlags flags) {
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

TEST(ConverterTests, AliasOfProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};
alias foo = P;
alias bar = request<P>;
alias baz = array<P>:4;
alias quux = vector<request<P>>:4;
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};
alias foo = client_end:P;
alias bar = server_end:P;
alias baz = array<client_end:P,4>;
alias quux = vector<server_end:P>:4;
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

TEST(ConverterTests, StructWithDefault) {
  std::string old_version = R"FIDL(
library example;

struct S {
  int32 a = 5;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type S = struct {
  a int32 = 5;
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
  o box<O>:optional;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

struct S {
  P p;
  P? po;
  request<P> r;
  request<P>? ro;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type S = struct {
  p client_end:P;
  po client_end:<optional,P>;
  r server_end:P;
  ro server_end:<optional,P>;
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

  std::string new_version = R"FIDL(
library example;

type T = table {};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithMember) {
  std::string old_version = R"FIDL(
library example;

table T {
  4: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type T = table {
  4: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

table T {
  1: P p;
  2: request<P> r;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type T = table {
  1: p client_end:P;
  2: r server_end:P;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
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

  std::string new_version = R"FIDL(
library example;

type T = table {
  1: v1 vector<uint8>;
  2: v2 vector<array<uint8,4>>:16;
  3: v3 vector<vector<array<uint8,4>>:<optional,16>>:32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithHandleWithSubtype) {
  std::string old_version = R"FIDL(
library example;

resource table T {
  1: handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type T = resource table {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

resource table T {
  1: handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type T = resource table {
  1: h handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
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

  std::string new_version = R"FIDL(
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
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberUnmodified) {
  std::string old_version = R"FIDL(
library example;

union U {
  1: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberFlexible) {
  std::string old_version = R"FIDL(
library example;

flexible union U {
  1: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = flexible union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithMemberStrict) {
  std::string old_version = R"FIDL(
library example;

strict union U {
  1: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = strict union {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

union U {
  1: P p;
  2: request<P> r;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type U = union {
  1: p client_end:P;
  2: r server_end:P;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
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

  std::string new_version = R"FIDL(
library example;

type U = union {
  1: v1 vector<uint8>;
  2: v2 vector<array<uint8,4>>:16;
  3: v3 vector<vector<array<uint8,4>>:<optional,16>>:32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeUnmodified) {
  std::string old_version = R"FIDL(
library example;

resource union U {
  1: handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = resource union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeFlexible) {
  std::string old_version = R"FIDL(
library example;

resource flexible union U {
  1: handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = resource flexible union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeStrict) {
  std::string old_version = R"FIDL(
library example;

resource strict union U {
  1: handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = resource strict union {
  1: h handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

resource union U {
  1: handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = resource union {
  1: h handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
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

  std::string new_version = R"FIDL(
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
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
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

TEST(ConverterTests, TypesInline) {
  std::string old_version = R"FIDL(
library example;

bits B {
  BM = 1;
};
enum E : uint64 {
  EM = 1;
};
table T {
  1: string TM;
};
strict union U {
  1: string UM;
};
struct S {};
protocol P {};

resource struct Foo {
  array<uint8>:4 a1;
  array<B>:4 a2;
  array<S?>:4 a3;
  bytes? b1;
  string? b2;
  vector<E>:16 v1;
  vector<T>:16 v2;
  vector<U>:16? v3;
  P p1;
  P? p2;
  request<P> r1;
  request<P>? r2;
  handle? h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type B = bits {
  BM = 1;
};
type E = enum : uint64 {
  EM = 1;
};
type T = table {
  1: TM string;
};
type U = strict union {
  1: UM string;
};
type S = struct {};
protocol P {};

type Foo = resource struct {
  a1 array<uint8,4>;
  a2 array<B,4>;
  a3 array<box<S>:optional,4>;
  b1 bytes:optional;
  b2 string:optional;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 vector<U>:<optional,16>;
  p1 client_end:P;
  p2 client_end:<optional,P>;
  r1 server_end:P;
  r2 server_end:<optional,P>;
  h1 handle:optional;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}
// One can name top-level FIDL types using names previously used for built-in
// types (for example, a struct called "uint16").  This test ensures that the
// converter is not fooled by such shenanigans.
TEST(ConverterTests, TypesConfusing) {
  std::string old_version = R"FIDL(
library example;

bits bool {
  BM = 1;
};
enum int8 : uint64 {
  EM = 1;
};
table int16 {
  1: string TM;
};
strict union uint8 {
  1: string UM;
};
struct uint16 {};
protocol uint32 {};
alias int32 = handle;
alias uint64 = bytes;
alias handle = string;

resource struct Foo {
  array<int64>:4 a1;
  array<bool>:4 a2;
  array<uint16?>:4 a3;
  uint64? b1;
  handle? b2;
  vector<int8>:16 v1;
  vector<int16>:16 v2;
  vector<uint8>:16? v3;
  uint32 p1;
  uint32? p2;
  request<uint32> r1;
  request<uint32>? r2;
  int32? h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type bool = bits {
  BM = 1;
};
type int8 = enum : uint64 {
  EM = 1;
};
type int16 = table {
  1: TM string;
};
type uint8 = strict union {
  1: UM string;
};
type uint16 = struct {};
protocol uint32 {};
alias int32 = handle;
alias uint64 = bytes;
alias handle = string;

type Foo = resource struct {
  a1 array<int64,4>;
  a2 array<bool,4>;
  a3 array<box<uint16>:optional,4>;
  b1 uint64:optional;
  b2 handle:optional;
  v1 vector<int8>:16;
  v2 vector<int16>:16;
  v3 vector<uint8>:<optional,16>;
  p1 client_end:uint32;
  p2 client_end:<optional,uint32>;
  r1 server_end:uint32;
  r2 server_end:<optional,uint32>;
  h1 int32:optional;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, TypesBehindAlias) {
  std::string old_version = R"FIDL(
library example;

bits BB {
  BM = 1;
};
enum EE : uint64 {
  EM = 1;
};
table TT {
  1: string TM;
};
strict union UU {
  1: string UM;
};
struct SS {};
protocol PP {};

alias A = array<uint8>:4;
alias B = BB;
alias E = EE;
alias H = handle?;
alias P = PP;
alias S = SS;
alias T = TT;
alias U = UU;
alias V = vector<U>?;
alias Y = bytes?;
alias Z = string?;

resource struct Foo {
  A a1;
  array<B>:4 a2;
  array<S?>:4 a3;
  Y b1;
  Z b2;
  vector<E>:16 v1;
  vector<T>:16 v2;
  V:16 v3;
  P p1;
  P? p2;
  request<P> r1;
  request<P>? r2;
  H h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type BB = bits {
  BM = 1;
};
type EE = enum : uint64 {
  EM = 1;
};
type TT = table {
  1: TM string;
};
type UU = strict union {
  1: UM string;
};
type SS = struct {};
protocol PP {};

alias A = array<uint8,4>;
alias B = BB;
alias E = EE;
alias H = handle:optional;
alias P = client_end:PP;
alias S = SS;
alias T = TT;
alias U = UU;
alias V = vector<U>:optional;
alias Y = bytes:optional;
alias Z = string:optional;

type Foo = resource struct {
  a1 A;
  a2 array<B,4>;
  a3 array<box<S>:optional,4>;
  b1 Y;
  b2 Z;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 V:16;
  p1 P;
  p2 P:optional;
  r1 server_end:P;
  r2 server_end:<optional,P>;
  h1 H;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, TypesBehindTwoAliases) {
  std::string old_version = R"FIDL(
library example;

bits BBB {
  BM = 1;
};
enum EEE : uint64 {
  EM = 1;
};
table TTT {
  1: string TM;
};
strict union UUU {
  1: string UM;
};
struct SSS {};
protocol PPP {};

alias AA = array<uint8>:4;
alias BB = BBB;
alias EE = EEE;
alias HH = handle?;
alias PP = PPP;
alias SS = SSS;
alias TT = TTT;
alias UU = UUU;
alias VV = vector<UU>?;
alias YY = bytes?;
alias ZZ = string?;

alias A = AA;
alias B = BB;
alias E = EE;
alias H = HH;
alias P = PP;
alias S = SS;
alias T = TT;
alias U = UU;
alias V = VV;
alias Y = YY;
alias Z = ZZ;

resource struct Foo {
  A a1;
  array<B>:4 a2;
  array<S?>:4 a3;
  Y b1;
  Z b2;
  vector<E>:16 v1;
  vector<T>:16 v2;
  V:16 v3;
  P p1;
  P? p2;
  request<P> r1;
  request<P>? r2;
  H h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type BBB = bits {
  BM = 1;
};
type EEE = enum : uint64 {
  EM = 1;
};
type TTT = table {
  1: TM string;
};
type UUU = strict union {
  1: UM string;
};
type SSS = struct {};
protocol PPP {};

alias AA = array<uint8,4>;
alias BB = BBB;
alias EE = EEE;
alias HH = handle:optional;
alias PP = client_end:PPP;
alias SS = SSS;
alias TT = TTT;
alias UU = UUU;
alias VV = vector<UU>:optional;
alias YY = bytes:optional;
alias ZZ = string:optional;

alias A = AA;
alias B = BB;
alias E = EE;
alias H = HH;
alias P = PP;
alias S = SS;
alias T = TT;
alias U = UU;
alias V = VV;
alias Y = YY;
alias Z = ZZ;

type Foo = resource struct {
  a1 A;
  a2 array<B,4>;
  a3 array<box<S>:optional,4>;
  b1 Y;
  b2 Z;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 V:16;
  p1 P;
  p2 P:optional;
  r1 server_end:P;
  r2 server_end:<optional,P>;
  h1 H;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, TypesBehindImport) {
  std::string dep1 = R"FIDL(
library dep1;

bits B {
  BM = 1;
};
enum E : uint64 {
  EM = 1;
};
table T {
  1: string TM;
};
strict union U {
  1: string UM;
};
struct S {};
protocol P {};

alias A = array<uint8>:4;
alias H = handle?;
alias V = vector<U>?;
alias Y = bytes?;
alias Z = string?;
)FIDL";

  std::string old_version = R"FIDL(
library example;

using dep1;

resource struct Foo {
  dep1.A a1;
  array<dep1.B>:4 a2;
  array<dep1.S?>:4 a3;
  dep1.Y b1;
  dep1.Z b2;
  vector<dep1.E>:16 v1;
  vector<dep1.T>:16 v2;
  dep1.V:16 v3;
  dep1.P p1;
  dep1.P? p2;
  request<dep1.P> r1;
  request<dep1.P>? r2;
  dep1.H h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using dep1;

type Foo = resource struct {
  a1 dep1.A;
  a2 array<dep1.B,4>;
  a3 array<box<dep1.S>:optional,4>;
  b1 dep1.Y;
  b2 dep1.Z;
  v1 vector<dep1.E>:16;
  v2 vector<dep1.T>:16;
  v3 dep1.V:16;
  p1 client_end:dep1.P;
  p2 client_end:<optional,dep1.P>;
  r1 server_end:dep1.P;
  r2 server_end:<optional,dep1.P>;
  h1 dep1.H;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, deps, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, deps, flags));
}

TEST(ConverterTests, TypesBehindTwoImports) {
  std::string dep1 = R"FIDL(
library dep1;

bits B {
  BM = 1;
};
enum E : uint64 {
  EM = 1;
};
table T {
  1: string TM;
};
strict union U {
  1: string UM;
};
struct S {};
protocol P {};

alias A = array<uint8>:4;
alias H = handle?;
alias V = vector<U>?;
alias Y = bytes?;
alias Z = string?;
)FIDL";

  std::string dep2 = R"FIDL(
library dep2;

using dep1 as imported;

alias A = imported.A;
alias B = imported.B;
alias E = imported.E;
alias H = imported.H;
alias P = imported.P;
alias S = imported.S;
alias T = imported.T;
alias U = imported.U;
alias V = imported.V;
alias Y = imported.Y;
alias Z = imported.Z;
)FIDL";

  std::string old_version = R"FIDL(
library example;

using dep2;

resource struct Foo {
  dep2.A a1;
  array<dep2.B>:4 a2;
  array<dep2.S?>:4 a3;
  dep2.Y b1;
  dep2.Z b2;
  vector<dep2.E>:16 v1;
  vector<dep2.T>:16 v2;
  dep2.V:16 v3;
  dep2.P p1;
  dep2.P? p2;
  request<dep2.P> r1;
  request<dep2.P>? r2;
  dep2.H h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using dep2;

type Foo = resource struct {
  a1 dep2.A;
  a2 array<dep2.B,4>;
  a3 array<box<dep2.S>:optional,4>;
  b1 dep2.Y;
  b2 dep2.Z;
  v1 vector<dep2.E>:16;
  v2 vector<dep2.T>:16;
  v3 dep2.V:16;
  p1 dep2.P;
  p2 dep2.P:optional;
  r1 server_end:dep2.P;
  r2 server_end:<optional,dep2.P>;
  h1 dep2.H;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  deps.emplace_back(dep2);
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, deps, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, deps, flags));
}

}  // namespace
