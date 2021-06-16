// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/new_syntax_conversion.h>
#include <fidl/new_syntax_converter.h>
#include <zxtest/zxtest.h>

#include "test_library.h"

namespace {
std::string Convert(const std::string& source, const std::vector<std::string>& deps,
                    fidl::ExperimentalFlags flags, fidl::utils::Syntax syntax) {
  // Convert the test file, along with its deps, into a flat AST.
  auto flat_lib = WithLibraryZx(source, flags);

  for (size_t i = 0; i < deps.size(); i++) {
    std::string dep_name = "dep" + std::to_string(i + 1) + ".fidl";
    TestLibrary dependency(dep_name, deps[i], flat_lib.OwnedShared(), flags);
    if (!dependency.Compile()) {
      flat_lib.PrintReports();
      return "DEPENDENCY_COMPILATION_FAILED: " + dep_name;
    }
    flat_lib.AddDependentLibrary(std::move(dependency));
  }
  if (!flat_lib.Compile()) {
    flat_lib.PrintReports();
    return "LIBRARY_COMPILED_FAILED";
  }

  // Read the file again, and convert it into a raw AST.
  TestLibrary raw_lib(source, flags);
  std::unique_ptr<fidl::raw::File> ast;
  raw_lib.Parse(&ast);

  // Run the ConvertingTreeVisitor using the two previously generated ASTs.
  fidl::conv::ConvertingTreeVisitor visitor =
      fidl::conv::ConvertingTreeVisitor(syntax, flat_lib.library());
  visitor.OnFile(ast);
  return visitor.converted_output();
}

std::string ToOldSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::utils::Syntax::kOld);
}

std::string ToOldSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::utils::Syntax::kOld);
}

std::string ToOldSyntax(const std::string& in, const std::vector<std::string>& deps,
                        fidl::ExperimentalFlags flags) {
  return Convert(in, deps, flags, fidl::utils::Syntax::kOld);
}

std::string ToNewSyntax(const std::string& in) {
  fidl::ExperimentalFlags flags;
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::utils::Syntax::kNew);
}

std::string ToNewSyntax(const std::string& in, fidl::ExperimentalFlags flags) {
  std::vector<std::string> deps;
  return Convert(in, deps, flags, fidl::utils::Syntax::kNew);
}

std::string ToNewSyntax(const std::string& in, const std::vector<std::string>& deps,
                        fidl::ExperimentalFlags flags) {
  return Convert(in, deps, flags, fidl::utils::Syntax::kNew);
}

// Even though "Deprecated" is technically not an official attribute, it is used
// often enough in the codebase to be included here.
TEST(ConverterTests, AttributesSingletons) {
  std::string old_version = R"FIDL(
[NoDoc]
library example;

[NoDoc]
const string C = "foo";

[Deprecated = "Reason"]
flexible enum E {
  A = 1;
  [Unknown] B = 2;
};

[MaxBytes = "1"]
struct S {
  [Doc = "Foo"] bool foo = false;
};

[MaxHandles = "2"]
union U {
  [Doc = "Foo"]
  1: bool foo;
};

[Discoverable]
protocol P1 {
  [Internal]
  M1();
};

[ForDeprecatedCBindings]
protocol P2 {
  [Selector = "Bar"] M2();
};

[Transport = "Syscall"]
protocol P3 {
  [Transitional] M3([Foo = "Bar"] bool b, [Baz = "Qux"] int8 c);
};

[NoDoc]
service X {
  [NoDoc]
  P1 p;
};
)FIDL";

  std::string new_version = R"FIDL(
@no_doc
library example;

@no_doc
const C string = "foo";

@deprecated("Reason")
type E = flexible enum {
  A = 1;
  @unknown B = 2;
};

@max_bytes("1")
type S = struct {
  @doc("Foo") foo bool = false;
};

@max_handles("2")
type U = strict union {
  @doc("Foo")
  1: foo bool;
};

@discoverable
protocol P1 {
  @internal
  M1();
};

@for_deprecated_c_bindings
protocol P2 {
  @selector("Bar") M2();
};

@transport("Syscall")
protocol P3 {
  @transitional M3(struct { @foo("Bar") b bool; @baz("Qux") c int8; });
};

@no_doc
service X {
  @no_doc
  p client_end:P1;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AttributesSingletonsUnofficial) {
  std::string old_version = R"FIDL(
[NoDoc2]
library example;

[NoDoc2]
const string C = "foo";

[Deprecated2 = "Reason"]
strict bits B {
  [Doc2 = "Foo"] A = 1;
};

[MaxBytes2 = "1"]
struct S {
  [Doc2 = "Foo"] bool foo = false;
};

[MaxHandles2 = "2"]
union U {
  [Doc2 = "Foo"]
  1: bool foo;
};

[Discoverable2]
protocol P1 {
  [Internal2]
  M1();
};

[ForDeprecatedCBindings2]
protocol P2 {
  [OnCompose] compose P1;
  [Selector2 = "Bar"] M2();
};

[Transport2 = "Syscall"]
protocol P3 {
  [Transitional2] M3([Foo = "Bar"] bool b, [Baz = "Qux"] int8 c);
};

[NoDoc2]
service X {
  [NoDoc2]
  P1 p;
};
)FIDL";

  std::string new_version = R"FIDL(
@no_doc2
library example;

@no_doc2
const C string = "foo";

@deprecated2("Reason")
type B = strict bits {
  @doc2("Foo") A = 1;
};

@max_bytes2("1")
type S = struct {
  @doc2("Foo") foo bool = false;
};

@max_handles2("2")
type U = strict union {
  @doc2("Foo")
  1: foo bool;
};

@discoverable2
protocol P1 {
  @internal2
  M1();
};

@for_deprecated_c_bindings2
protocol P2 {
  @on_compose compose P1;
  @selector2("Bar") M2();
};

@transport2("Syscall")
protocol P3 {
  @transitional2 M3(struct { @foo("Bar") b bool; @baz("Qux") c int8; });
};

@no_doc2
service X {
  @no_doc2
  p client_end:P1;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

// The converter suffers from a slight inconsistency where the whitespace
// between a doc comment and a subsequent attribute block is replaced with a
// single newline.  For example, "///Foo\n\s\s\s\s[Bar]" becomes "///Foo\n@bar"
// post-conversion.  This is not a major issue, as the formatter for the new
// syntax will fix such irregularities post-conversion.
TEST(ConverterTests, AttributesSingletonsWithDocComments) {
  std::string old_version = R"FIDL(
/// For example
[NoDoc]
library example;

/// For C
[NoDoc]
const string C = "foo";

/// For E
[Deprecated = "Reason"]
flexible enum E {
  A = 1;
  /// For B
[Unknown] B = 2;
};

/// For S
[MaxBytes = "1"]
struct S {
[Doc = "Foo"] bool foo = false;
};

/// For T
[MaxHandles = "2"]
table T {
[Doc = "Foo"]
  1: bool foo;
};

/// For P1
[Discoverable]
protocol P1 {
  /// For M1
[Internal]
  M1();
};

/// For P2
[ForDeprecatedCBindings]
protocol P2 {
  /// Compose P1
[OnCompose] compose P1;
  /// For M2
[Selector = "Bar"] M2();
};

/// For P3
[Transport = "Syscall"]
protocol P3 {
  /// For M3
[Transitional] M3([Foo = "Bar"] bool b, [Baz = "Qux"] int8 c);
};

/// For X
[NoDoc]
service X {
  /// For P1
[NoDoc]
  P1 p;
};
)FIDL";

  std::string new_version = R"FIDL(
/// For example
@no_doc
library example;

/// For C
@no_doc
const C string = "foo";

/// For E
@deprecated("Reason")
type E = flexible enum {
  A = 1;
  /// For B
@unknown B = 2;
};

/// For S
@max_bytes("1")
type S = struct {
@doc("Foo") foo bool = false;
};

/// For T
@max_handles("2")
type T = table {
@doc("Foo")
  1: foo bool;
};

/// For P1
@discoverable
protocol P1 {
  /// For M1
@internal
  M1();
};

/// For P2
@for_deprecated_c_bindings
protocol P2 {
  /// Compose P1
@on_compose compose P1;
  /// For M2
@selector("Bar") M2();
};

/// For P3
@transport("Syscall")
protocol P3 {
  /// For M3
@transitional M3(struct { @foo("Bar") b bool; @baz("Qux") c int8; });
};

/// For X
@no_doc
service X {
  /// For P1
@no_doc
  p client_end:P1;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AttributesLists) {
  std::string old_version = R"FIDL(
library example;

[NoDoc, Deprecated = "Note"]
const string C = "foo";

[Deprecated = "Reason", Transitional]
enum E {
  A = 1;
  [Doc = "Foo", Unknown] B = 2;
};

[MaxBytes = "1", MaxHandles = "2"]
resource struct S {};

[Discoverable, ForDeprecatedCBindings, Transport = "Syscall"]
protocol P {
  [Internal, Selector = "Bar", Transitional] M();
};

[Doc = "X", NoDoc]
service X {
  [Doc = "P", NoDoc]
  P p;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

@no_doc @deprecated("Note")
const C string = "foo";

@deprecated("Reason") @transitional
type E = strict enum {
  A = 1;
  @doc("Foo") @unknown B = 2;
};

@max_bytes("1") @max_handles("2")
type S = resource struct {};

@discoverable @for_deprecated_c_bindings @transport("Syscall")
protocol P {
  @internal @selector("Bar") @transitional M();
};

@doc("X") @no_doc
service X {
  @doc("P") @no_doc
  p client_end:P;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AttributesListsUnofficial) {
  std::string old_version = R"FIDL(
library example;

[NoDoc2, Deprecated2 = "Note"]
const string C = "foo";

[Deprecated2 = "Reason", Transitional2]
enum E {
  A = 1;
  [Doc2 = "Foo", Unknown2] B = 2;
};

[MaxBytes2 = "1", MaxHandles2 = "2"]
resource struct S {};

[Discoverable2, ForDeprecatedCBindings2, Transport2 = "Syscall"]
protocol P {
  [Internal2, Selector2 = "Bar", Transitional2] M();
};

[Doc2 = "X", NoDoc2]
service X {
  [Doc2 = "P", NoDoc2]
  P p;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

@no_doc2 @deprecated2("Note")
const C string = "foo";

@deprecated2("Reason") @transitional2
type E = strict enum {
  A = 1;
  @doc2("Foo") @unknown2 B = 2;
};

@max_bytes2("1") @max_handles2("2")
type S = resource struct {};

@discoverable2 @for_deprecated_c_bindings2 @transport2("Syscall")
protocol P {
  @internal2 @selector2("Bar") @transitional2 M();
};

@doc2("X") @no_doc2
service X {
  @doc2("P") @no_doc2
  p client_end:P;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AttributesListsWithDocComments) {
  std::string old_version = R"FIDL(
library example;

/// For C
[NoDoc, Deprecated = "Note"]
const string C = "foo";

/// For E
[Deprecated = "Reason", Transitional]
enum E {
  A = 1;
  /// For B
[Unknown] B = 2;
};

/// For S
[MaxBytes = "1", MaxHandles = "2"]
resource struct S {};

/// For P
[Discoverable, ForDeprecatedCBindings, Transport = "Syscall"]
protocol P {
  /// For M
[Internal, Selector = "Bar", Transitional] M();
};

/// For X
[Foo = "X", NoDoc]
service X {
  /// For P
[Foo = "P", NoDoc]
  P p;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// For C
@no_doc @deprecated("Note")
const C string = "foo";

/// For E
@deprecated("Reason") @transitional
type E = strict enum {
  A = 1;
  /// For B
@unknown B = 2;
};

/// For S
@max_bytes("1") @max_handles("2")
type S = resource struct {};

/// For P
@discoverable @for_deprecated_c_bindings @transport("Syscall")
protocol P {
  /// For M
@internal @selector("Bar") @transitional M();
};

/// For X
@foo("X") @no_doc
service X {
  /// For P
@foo("P") @no_doc
  p client_end:P;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
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

TEST(ConverterTests, ParameterBecomesConstraint) {
  std::string old_version = R"FIDL(
library example;

protocol MyProtocol {};
resource struct Foo {
  MyProtocol? b;
  request<MyProtocol>? d;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol MyProtocol {};
type Foo = resource struct {
  b client_end:<MyProtocol,optional>;
  d server_end:<MyProtocol,optional>;
};
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

alias foo = zx.handle:<VMO,optional>;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, AliasOfHandleWithSubtypeAndRights) {
  std::string old_version = R"FIDL(
library example;

using zx;

alias foo = zx.handle:<VMO,zx.rights.DUPLICATE | zx.rights.TRANSFER>?;
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

alias foo = zx.handle:<VMO,zx.rights.DUPLICATE | zx.rights.TRANSFER,optional>;
)FIDL";

  fidl::ExperimentalFlags flags;

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

alias foo = vector<vector<array<uint8,5>>:optional>:<9,optional>;
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
  BIGGEST = 0x80000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict bits {
  SMALLEST = 1;
  BIGGEST = 0x80000000;
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
  BIGGEST = 0x80000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = flexible bits {
  SMALLEST = 1;
  BIGGEST = 0x80000000;
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
  BIGGEST = 0x80000000;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

/// Doc comment.
type Foo = strict bits {
  SMALLEST = 1;
  BIGGEST = 0x80000000;
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
type Foo = strict bits : uint64 {
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
type Foo = strict enum {
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
type Foo = strict enum : uint64 {
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
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(struct { a string; b int32; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolCompose) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b);
};

protocol Bar {
  /// Bar
  compose Foo;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(struct { a string; b int32; });
};

protocol Bar {
  /// Bar
  compose Foo;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolEmpty) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo() -> ();
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo() -> ();
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithEvent) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  -> DoFoo(bool a, uint8 b);
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  -> DoFoo(struct { a bool; b uint8; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithResponse) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b) -> (bool c, uint8 d);
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(struct { a string; b int32; }) -> (struct { c bool; d uint8; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithResponseAndError) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(string a, int32 b) -> (bool c, uint8 d) error int32;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo(struct { a string; b int32; }) -> (struct { c bool; d uint8; }) error int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

// Tests the special
TEST(ConverterTests, ProtocolEmptyWithResponseAndError) {
  std::string old_version = R"FIDL(
library example;

protocol Foo {
  DoFoo() -> () error int32;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol Foo {
  DoFoo() -> (struct { }) error int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithResource) {
  std::string old_version = R"FIDL(
library example;

using zx;

protocol Foo {
  DoFoo(zx.handle i) -> (zx.handle:VMO o);
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

protocol Foo {
  DoFoo(resource struct { i zx.handle; }) -> (resource struct { o zx.handle:VMO; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ProtocolWithTransitiveResource) {
  std::string old_version = R"FIDL(
library example;

resource table ResourceType {
  1: reserved;
};

protocol Foo {
  DoFoo(ResourceType data);
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type ResourceType = resource table {
  1: reserved;
};

protocol Foo {
  DoFoo(resource struct { data ResourceType; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ResourceDeclaration) {
  std::string old_version = R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
    };
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ServiceEmpty) {
  std::string old_version = R"FIDL(
library example;

service S {};
)FIDL";

  std::string new_version = R"FIDL(
library example;

service S {};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, ServiceWithMember) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

service S {
  P p;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

service S {
  p client_end:P;
};
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
  o box<O>;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, StructWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

resource struct S {
  P p;
  P? po;
  request<P> r;
  request<P>? ro;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type S = resource struct {
  p client_end:P;
  po client_end:<P,optional>;
  r server_end:P;
  ro server_end:<P,optional>;
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
  v3 vector<uint8>:<16,optional>;
  v4 vector<vector<uint8>:optional>:16;
  v5 vector<vector<vector<uint8>:<16,optional>>>:optional;
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
  zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  h zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER>;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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
  array<zx.handle:<PORT,zx.rights.DUPLICATE | zx.rights.TRANSFER>?>:5 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type S = resource struct {
  a array<zx.handle:<PORT,zx.rights.DUPLICATE | zx.rights.TRANSFER,optional>,5>;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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
using
// 4
zx
// 5
;

// 6
/// Foo
// 6a
[
// 6b
NoDoc
// 6c
]
// 6d
resource
// 7
// 8
struct
// 9
S
// 10
{
// 11
int32
// 12
a
// 13
;
// 14
/// Bar
vector
// 15
<
// 16
zx.handle
// 17
:
// 18
<
// 19
VMO
// 20
,
// 21
zx.rights.DUPLICATE
// 22
>
// 23
?
// 24
>
// 25
:
// 26
16
// 27
?
// 28
b
// 29
;
// 30
}
// 31
;
// 32
)FIDL";

  std::string new_version = R"FIDL(
// 0
library
// 1
example
// 2
;

// 3
using
// 4
zx
// 5
;

// 6
// 6a
// 6b
// 6c
/// Foo
@no_doc
// 6d
// 7
// 8
// 9
type S = resource struct
// 10
{
// 11
// 12
a int32
// 13
;
// 14
/// Bar
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
// 26
// 27
// 28
b vector<zx.handle:<VMO,zx.rights.DUPLICATE,optional>>:<16,optional>
// 29
;
// 30
}
// 31
;
// 32
)FIDL";

  fidl::ExperimentalFlags flags;

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
  1: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type T = table {
  1: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithReserved) {
  std::string old_version = R"FIDL(
library example;

table T {
  1: reserved;
  2: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type T = table {
  1: reserved;
  2: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, TableWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

resource table T {
  1: P p;
  2: request<P> r;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type T = resource table {
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
  3: v3 vector<vector<array<uint8,4>>:<16,optional>>:32;
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
  1: zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type T = resource table {
  1: h zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER>;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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

type U = strict union {
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

TEST(ConverterTests, UnionWithMemberReserved) {
  std::string old_version = R"FIDL(
library example;

flexible union U {
  1: reserved;
  2: int32 a;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type U = flexible union {
  1: reserved;
  2: a int32;
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, UnionWithProtocols) {
  std::string old_version = R"FIDL(
library example;

protocol P {};

resource union U {
  1: P p;
  2: request<P> r;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

protocol P {};

type U = strict resource union {
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

type U = strict union {
  1: v1 vector<uint8>;
  2: v2 vector<array<uint8,4>>:16;
  3: v3 vector<vector<array<uint8,4>>:<16,optional>>:32;
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

type U = strict resource union {
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

flexible resource union U {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = flexible resource union {
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

strict resource union U {
  1: zx.handle:VMO h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = strict resource union {
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
  1: zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type U = strict resource union {
  1: h zx.handle:<CHANNEL,zx.rights.DUPLICATE | zx.rights.TRANSFER>;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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
type U = strict union {
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
// Another Comment.
using zx;

// Comment.
/// Doc Comment.
alias foo = zx.handle;

/// Doc Comment.
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
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type B = strict bits {
  BM = 1;
};
type E = strict enum : uint64 {
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
  a3 array<box<S>,4>;
  b1 bytes:optional;
  b2 string:optional;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 vector<U>:<16,optional>;
  p1 client_end:P;
  p2 client_end:<P,optional>;
  r1 server_end:P;
  r2 server_end:<P,optional>;
  h1 zx.handle:optional;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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
  int8 = 1;
};
enum int8 : uint64 {
  bool = 1;
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
alias uint64 = bytes;

resource struct Foo {
  array<uint64>:4 a1;
  array<bool>:4 a2;
  array<uint16>:4 a3;
  uint64 b1;
  vector<int8>:16 v1;
  vector<int16>:16 v2;
  vector<uint8>:16? v3;
  uint32 p1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using zx;

type bool = strict bits {
  int8 = 1;
};
type int8 = strict enum : uint64 {
  bool = 1;
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
alias uint64 = bytes;

type Foo = resource struct {
  a1 array<uint64,4>;
  a2 array<bool,4>;
  a3 array<uint16,4>;
  b1 uint64;
  v1 vector<int8>:16;
  v2 vector<int16>:16;
  v3 vector<uint8>:<16,optional>;
  p1 client_end:uint32;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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

using zx;

type BB = strict bits {
  BM = 1;
};
type EE = strict enum : uint64 {
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
  a3 array<box<S>,4>;
  b1 Y;
  b2 Z;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 V:16;
  p1 P;
  p2 P:optional;
  r1 server_end:P;
  r2 server_end:<P,optional>;
  h1 H;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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

using zx;

type BBB = strict bits {
  BM = 1;
};
type EEE = strict enum : uint64 {
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
  a3 array<box<S>,4>;
  b1 Y;
  b2 Z;
  v1 vector<E>:16;
  v2 vector<T>:16;
  v3 V:16;
  p1 P;
  p2 P:optional;
  r1 server_end:P;
  r2 server_end:<P,optional>;
  h1 H;
};
)FIDL";

  fidl::ExperimentalFlags flags;

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
  a3 array<box<dep1.S>,4>;
  b1 dep1.Y;
  b2 dep1.Z;
  v1 vector<dep1.E>:16;
  v2 vector<dep1.T>:16;
  v3 dep1.V:16;
  p1 client_end:dep1.P;
  p2 client_end:<dep1.P,optional>;
  r1 server_end:dep1.P;
  r2 server_end:<dep1.P,optional>;
  h1 dep1.H;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  fidl::ExperimentalFlags flags;

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
  a3 array<box<dep2.S>,4>;
  b1 dep2.Y;
  b2 dep2.Z;
  v1 vector<dep2.E>:16;
  v2 vector<dep2.T>:16;
  v3 dep2.V:16;
  p1 dep2.P;
  p2 dep2.P:optional;
  r1 server_end:dep2.P;
  r2 server_end:<dep2.P,optional>;
  h1 dep2.H;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  deps.emplace_back(dep2);
  fidl::ExperimentalFlags flags;

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, deps, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, deps, flags));
}

TEST(ConverterTests, TypesBehindTwoAliasedImports) {
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

using dep2 as d2;

resource struct Foo {
  d2.A a1;
  array<d2.B>:4 a2;
  array<d2.S?>:4 a3;
  d2.Y b1;
  d2.Z b2;
  vector<d2.E>:16 v1;
  vector<d2.T>:16 v2;
  d2.V:16 v3;
  d2.P p1;
  d2.P? p2;
  request<d2.P> r1;
  request<d2.P>? r2;
  d2.H h1;
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using dep2 as d2;

type Foo = resource struct {
  a1 d2.A;
  a2 array<d2.B,4>;
  a3 array<box<d2.S>,4>;
  b1 d2.Y;
  b2 d2.Z;
  v1 vector<d2.E>:16;
  v2 vector<d2.T>:16;
  v3 d2.V:16;
  p1 d2.P;
  p2 d2.P:optional;
  r1 server_end:d2.P;
  r2 server_end:<d2.P,optional>;
  h1 d2.H;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  deps.emplace_back(dep2);
  fidl::ExperimentalFlags flags;

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
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

using dep1;

alias AA = dep1.A;
alias BB = dep1.B;
alias EE = dep1.E;
alias HH = dep1.H;
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
  a3 array<box<SS>,4>;
  b1 YY;
  b2 ZZ;
  v1 vector<EE>:16;
  v2 vector<TT>:16;
  v3 VV:16;
  p1 PP;
  p2 PP:optional;
  r1 server_end:PP;
  r2 server_end:<PP,optional>;
  h1 HH;
};
)FIDL";
  std::vector<std::string> deps;
  deps.emplace_back(dep1);
  fidl::ExperimentalFlags flags;

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, deps, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, deps, flags));
}

TEST(ConverterTests, AliasOfResource) {
  std::string old_version = R"FIDL(
library example;

resource struct Resource {};
alias MyResource = vector<Resource>;

protocol Foo {
  SendResource(MyResource r);
};
)FIDL";

  std::string new_version = R"FIDL(
library example;

type Resource = resource struct {};
alias MyResource = vector<Resource>;

protocol Foo {
  SendResource(resource struct { r MyResource; });
};
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version));
}

TEST(ConverterTests, DeprecatedSyntaxToken) {
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  std::string old_version = R"FIDL(deprecated_syntax;
library example;
)FIDL";

  std::string new_version = R"FIDL(
library example;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, DeprecatedSyntaxTokenAfterComment) {
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  std::string old_version = R"FIDL(
// Foo
deprecated_syntax;
library example;
)FIDL";

  std::string new_version = R"FIDL(
// Foo
library example;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

TEST(ConverterTests, DeprecatedSyntaxTokenWeird) {
  fidl::ExperimentalFlags flags;
  flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  std::string old_version = R"FIDL(
  deprecated_syntax  ;
library example;
)FIDL";

  std::string new_version = R"FIDL(
library example;
)FIDL";

  ASSERT_STR_EQ(old_version, ToOldSyntax(old_version, flags));
  ASSERT_STR_EQ(new_version, ToNewSyntax(old_version, flags));
}

}  // namespace
