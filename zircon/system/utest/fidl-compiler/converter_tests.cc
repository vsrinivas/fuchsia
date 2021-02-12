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

  // Include a fake "zx" library with every test.
  std::string zx = R"FIDL(
library zx;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};
)FIDL";
  TestLibrary zx_lib("zx.fidl", zx, &shared, flags);
  zx_lib.Compile();
  flat_lib.AddDependentLibrary(std::move(zx_lib));

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

using zx;

alias foo = zx.handle:VMO?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

alias foo = zx.handle:<optional,VMO>;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AliasOfHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

using zx;

alias foo = zx.handle:<VMO,1>?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

alias foo = zx.handle:<optional,VMO,1>;
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

using zx;

resource struct S {
  zx.handle? h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  h zx.handle:optional;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithHandleWithSubtype) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource struct S {
  zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  h zx.handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource struct S {
  zx.handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  h zx.handle:<CHANNEL,7>;
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
// contains an array type declaration, which itself contains a zx.handle type
// declaration that needs to be converted as well.
TEST(ConverterTests, StructWithManyNestedConversions) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource struct S {
  array<zx.handle:<PORT,7>?>:5 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  a array<zx.handle:<optional,PORT,7>,5>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, StructWithComments) {
  std::string old_version = R"FIDL(
// Library comment.
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
// Library comment.
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

// Make sure that comments inserted in weird gaps where we would not usually
// expect to see comments are not lost.  This test only applies to the new
// syntax - keeping comments in place for the old syntax is too cumbersome.
TEST(ConverterTests, StructWithCommentsSilly) {
  std::string old_version = R"FIDL(
// 0
library
// 1
example
// 2
;

// 3
resource
// 4
// 5
struct
// 6
S
// 7
{
// 8
int32
// 9
a
// 10
;
// 11
vector
// 12
<
// 13
handle
// 14
:
// 15
<
// 16
VMO
// 17
,
// 18
7
// 19
>
// 20
?
// 21
>
// 22
:
// 23
16
// 24
?
// 25
b
// 26
;
// 27
}
// 28
;
// 29
)FIDL";

  std::string new_version = R"FIDL(
// 0
library
// 1
example
// 2
;

// 3
// 4
// 5
// 6
type S = resource struct
// 7
{
// 8
// 9
a int32
// 10
;
// 11
// 12
// 13
// 14
// 15
// 16
// 17
// 18
// 19
// 20
// 21
// 22
// 23
// 24
// 25
b vector<handle:<optional,VMO,7>>:<optional,16>
// 26
;
// 27
}
// 28
;
// 29
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
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

using zx;

resource table T {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type T = resource table {
  1: h zx.handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource table T {
  1: zx.handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type T = resource table {
  1: h zx.handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, TableWithComments) {
  std::string old_version = R"FIDL(
// Library comment.
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
// Library comment.
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

using zx;

resource union U {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = resource union {
  1: h zx.handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeFlexible) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource flexible union U {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = resource flexible union {
  1: h zx.handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeStrict) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource strict union U {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = resource strict union {
  1: h zx.handle:VMO;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

using zx;

resource union U {
  1: zx.handle:<CHANNEL,7> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = resource union {
  1: h zx.handle:<CHANNEL,7>;
};
)FIDL";

  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, UnionWithComments) {
  std::string old_version = R"FIDL(
// Library comment.
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
// Library comment.
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

using zx;

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
  zx.handle? h1;
  handle h2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

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
  h1 zx.handle:optional;
  h2 handle;
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

using zx;

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
alias int32 = zx.handle;
alias int64 = handle;
alias uint64 = bytes;
alias handle = string;

resource struct Foo {
  array<uint64>:4 a1;
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
  int64 h2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

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
alias int32 = zx.handle;
alias int64 = handle;
alias uint64 = bytes;
alias handle = string;

type Foo = resource struct {
  a1 array<uint64,4>;
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
  h2 int64;
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

using zx;

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
alias H = zx.handle?;
alias I = handle;
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
  I h2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

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
alias H = zx.handle:optional;
alias I = handle;
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
  h2 I;
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

using zx;

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
alias HH = zx.handle?;
alias II = handle;
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
alias I = II;
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
  I h2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

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
alias HH = zx.handle:optional;
alias II = handle;
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
alias I = II;
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
  h2 I;
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

using zx;

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
alias H = zx.handle?;
alias I = handle;
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
  dep1.I h2;
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
  h2 dep1.I;
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

using zx;

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
alias H = zx.handle?;
alias I = handle;
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
alias I = imported.I;
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
  dep2.I h2;
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
  h2 dep2.I;
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

TEST(ConverterTests, TypesBehindAliasThenImport) {
  std::string dep1 = R"FIDL(
library dep1;

using zx;

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
alias H = zx.handle?;
alias I = handle;
alias V = vector<U>?;
alias Y = bytes?;
alias Z = string?;
)FIDL";

  std::string old_version = R"FIDL(
library example;

using dep1;

alias AA = dep1.A;
alias BB = dep1.B;
alias EE = dep1.E;
alias HH = dep1.H;
alias II = dep1.I;
alias PP = dep1.P;
alias SS = dep1.S;
alias TT = dep1.T;
alias UU = dep1.U;
alias VV = dep1.V;
alias YY = dep1.Y;
alias ZZ = dep1.Z;

resource struct Foo {
  AA a1;
  array<BB>:4 a2;
  array<SS?>:4 a3;
  YY b1;
  ZZ b2;
  vector<EE>:16 v1;
  vector<TT>:16 v2;
  VV:16 v3;
  PP p1;
  PP? p2;
  request<PP> r1;
  request<PP>? r2;
  HH h1;
  II h2;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using dep1;

alias AA = dep1.A;
alias BB = dep1.B;
alias EE = dep1.E;
alias HH = dep1.H;
alias II = dep1.I;
alias PP = client_end:dep1.P;
alias SS = dep1.S;
alias TT = dep1.T;
alias UU = dep1.U;
alias VV = dep1.V;
alias YY = dep1.Y;
alias ZZ = dep1.Z;

type Foo = resource struct {
  a1 AA;
  a2 array<BB,4>;
  a3 array<box<SS>:optional,4>;
  b1 YY;
  b2 ZZ;
  v1 vector<EE>:16;
  v2 vector<TT>:16;
  v3 VV:16;
  p1 PP;
  p2 PP:optional;
  r1 server_end:PP;
  r2 server_end:<optional,PP>;
  h1 HH;
  h2 II;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, deps, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, deps, flags));
}

}  // namespace
