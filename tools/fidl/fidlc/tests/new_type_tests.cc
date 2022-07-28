// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace fidl {

namespace {

TEST(NewTypeTests, GoodNewTypes) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  bytes vector<uint8>;
};

type OpaqueFoo = Foo;

type Bar = enum {
  PARALLEL = 0;
  PERPENDICULAR = 1;
};

type OpaqueBar = Bar;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, GoodNewTypesResourceness) {
  TestLibrary library(R"FIDL(
library example;

type A = resource struct {};
type B = A;
type C = resource struct { b B; };
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, BadNewTypesResourceness) {
  TestLibrary library(R"FIDL(
library example;

type A = resource struct {};
type B = A;
type C = struct { b B; };
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTypeMustBeResource);
}

TEST(NewTypeTests, GoodNewTypesSimple) {
  TestLibrary library(R"FIDL(
library example;

type Bits = bits { A = 1; };
type Enum = enum {
  A = 1;
  B = 15;
};
type Struct = struct { foo string; };
type Table = table {};
type Union = union { 1: b bool; };
alias Alias = Struct;

// Now for the new-types
type NewBits = Bits;
type NewEnum = Enum;
type NewStruct = Struct;
type NewTable = Table;
type NewUnion = Union;
type NewAlias = Alias;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, GoodNewTypesBuiltin) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type Struct = struct {};
protocol Protocol {};

type NewBool = bool;
type NewInt = int16;
type NewString = string;
type NewArray = array<int8, 3>;
type NewVector = vector<bool>;
type NewBox = box<Struct>;
type NewHandle = zx.handle;
type NewClientEnd = client_end:Protocol;
type NewServerEnd = server_end:Protocol;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, GoodNewTypesComplex) {
  TestLibrary library(R"FIDL(
library example;

type Struct = struct {};
type NewStruct = Struct;
type DoubleNewStruct = NewStruct;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, GoodNewTypesConstrained) {
  TestLibrary library(R"FIDL(
library example;

type ConstrainedVec = vector<int32>:<5, optional>;
type ConstrainedString = string:108;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_COMPILED(library);
}

TEST(NewTypeTests, BadNewTypesConstraints) {
  TestLibrary library(R"FIDL(
library example;

type NewVec = vector<int32>;
type OptionalNewVec = NewVec:optional;
)FIDL");

  library.EnableFlag(ExperimentalFlags::Flag::kAllowNewTypes);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNewTypeCannotHaveConstraint);
}

}  // namespace

}  // namespace fidl
