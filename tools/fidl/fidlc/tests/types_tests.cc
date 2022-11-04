// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/types.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace fidl::flat {

static std::optional<types::PrimitiveSubtype> ConstPrimitiveSubtype(TestLibrary& library,
                                                                    const char* const_name) {
  auto type = library.LookupConstant(const_name)->value->type;
  if (type->kind == Type::Kind::kPrimitive) {
    return static_cast<const PrimitiveType*>(type)->subtype;
  }
  return std::nullopt;
}

TEST(TypesTests, GoodRootTypesUnqualified) {
  TestLibrary library(R"FIDL(
library example;

const b bool = false;
const i8 int8 = 0;
const i16 int16 = 0;
const i32 int32 = 0;
const i64 int64 = 0;
const u8 uint8 = 0;
const u16 uint16 = 0;
const u32 uint32 = 0;
const u64 uint64 = 0;
const us usize = 0;
const up uintptr = 0;
const uc uchar = 0;
const f32 float32 = 0;
const f64 float64 = 0;
)FIDL");

  // For the use of usize, uintptr, and uchar.
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kZxCTypes);

  ASSERT_COMPILED(library);

  EXPECT_EQ(ConstPrimitiveSubtype(library, "b"), types::PrimitiveSubtype::kBool);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "i8"), types::PrimitiveSubtype::kInt8);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "i16"), types::PrimitiveSubtype::kInt16);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "i32"), types::PrimitiveSubtype::kInt32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "i64"), types::PrimitiveSubtype::kInt64);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "u8"), types::PrimitiveSubtype::kUint8);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "u16"), types::PrimitiveSubtype::kUint16);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "u32"), types::PrimitiveSubtype::kUint32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "u64"), types::PrimitiveSubtype::kUint64);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "us"), types::PrimitiveSubtype::kZxUsize);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "up"), types::PrimitiveSubtype::kZxUintptr);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uc"), types::PrimitiveSubtype::kZxUchar);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "f32"), types::PrimitiveSubtype::kFloat32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "f64"), types::PrimitiveSubtype::kFloat64);
}

TEST(TypesTests, GoodRootTypesQualified) {
  TestLibrary library(R"FIDL(
library example;

const bool fidl.bool = false;
const int8 fidl.int8 = 0;
const int16 fidl.int16 = 0;
const int32 fidl.int32 = 0;
const int64 fidl.int64 = 0;
const uint8 fidl.uint8 = 0;
const uint16 fidl.uint16 = 0;
const uint32 fidl.uint32 = 0;
const uint64 fidl.uint64 = 0;
const usize fidl.usize = 0;
const uintptr fidl.uintptr = 0;
const uchar fidl.uchar = 0;
const float32 fidl.float32 = 0;
const float64 fidl.float64 = 0;
)FIDL");

  // For the use of usize, uintptr, and uchar.
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kZxCTypes);

  ASSERT_COMPILED(library);

  EXPECT_EQ(ConstPrimitiveSubtype(library, "bool"), types::PrimitiveSubtype::kBool);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "int8"), types::PrimitiveSubtype::kInt8);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "int16"), types::PrimitiveSubtype::kInt16);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "int32"), types::PrimitiveSubtype::kInt32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "int64"), types::PrimitiveSubtype::kInt64);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uint8"), types::PrimitiveSubtype::kUint8);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uint16"), types::PrimitiveSubtype::kUint16);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uint32"), types::PrimitiveSubtype::kUint32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uint64"), types::PrimitiveSubtype::kUint64);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "usize"), types::PrimitiveSubtype::kZxUsize);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uintptr"), types::PrimitiveSubtype::kZxUintptr);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "uchar"), types::PrimitiveSubtype::kZxUchar);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "float32"), types::PrimitiveSubtype::kFloat32);
  EXPECT_EQ(ConstPrimitiveSubtype(library, "float64"), types::PrimitiveSubtype::kFloat64);
}

// Check that fidl's types.h and zircon/types.h's handle subtype
// values stay in sync, until the latter is generated.
TEST(TypesTests, GoodHandleSubtype) {
  static_assert(sizeof(types::HandleSubtype) == sizeof(zx_obj_type_t));

  static_assert(types::HandleSubtype::kHandle ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_NONE));

  static_assert(types::HandleSubtype::kBti == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_BTI));
  static_assert(types::HandleSubtype::kChannel ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_CHANNEL));
  static_assert(types::HandleSubtype::kClock ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_CLOCK));
  static_assert(types::HandleSubtype::kEvent ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EVENT));
  static_assert(types::HandleSubtype::kEventpair ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EVENTPAIR));
  static_assert(types::HandleSubtype::kException ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_EXCEPTION));
  static_assert(types::HandleSubtype::kFifo == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_FIFO));
  static_assert(types::HandleSubtype::kGuest ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_GUEST));
  static_assert(types::HandleSubtype::kInterrupt ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_INTERRUPT));
  static_assert(types::HandleSubtype::kIommu ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_IOMMU));
  static_assert(types::HandleSubtype::kJob == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_JOB));
  static_assert(types::HandleSubtype::kLog == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_LOG));
  static_assert(types::HandleSubtype::kPager ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PAGER));
  static_assert(types::HandleSubtype::kPciDevice ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PCI_DEVICE));
  static_assert(types::HandleSubtype::kPmt == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PMT));
  static_assert(types::HandleSubtype::kPort == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PORT));
  static_assert(types::HandleSubtype::kProcess ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PROCESS));
  static_assert(types::HandleSubtype::kProfile ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_PROFILE));
  static_assert(types::HandleSubtype::kResource ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_RESOURCE));
  static_assert(types::HandleSubtype::kSocket ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_SOCKET));
  static_assert(types::HandleSubtype::kStream ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_STREAM));
  static_assert(types::HandleSubtype::kSuspendToken ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_SUSPEND_TOKEN));
  static_assert(types::HandleSubtype::kThread ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_THREAD));
  static_assert(types::HandleSubtype::kTimer ==
                static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_TIMER));
  static_assert(types::HandleSubtype::kVcpu == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VCPU));
  static_assert(types::HandleSubtype::kVmar == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VMAR));
  static_assert(types::HandleSubtype::kVmo == static_cast<types::HandleSubtype>(ZX_OBJ_TYPE_VMO));
}

// Check that fidl's types.h and zircon/types.h's rights types stay in
// sync, until the latter is generated.
TEST(TypesTests, GoodRights) {
  static_assert(sizeof(types::RightsWrappedType) == sizeof(zx_rights_t));
}

TEST(NewSyntaxTests, GoodTypeDeclOfAnonymousLayouts) {
  TestLibrary library(R"FIDL(
library example;
type TypeDecl = struct {
    f0 bits {
      FOO = 1;
    };
    f1 enum {
      BAR = 1;
    };
    f2 struct {
      i0 vector<uint8>;
      @allow_deprecated_struct_defaults
      i1 string = "foo";
    };
    f3 table {
      1: i0 bool;
    };
    f4 union {
      1: i0 bool;
    };
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 5);
  auto type_decl_f0 = library.LookupBits("F0");
  ASSERT_NOT_NULL(type_decl_f0);
  EXPECT_EQ(type_decl_f0->members.size(), 1);
  auto type_decl_f1 = library.LookupEnum("F1");
  ASSERT_NOT_NULL(type_decl_f1);
  EXPECT_EQ(type_decl_f1->members.size(), 1);
  auto type_decl_f2 = library.LookupStruct("F2");
  ASSERT_NOT_NULL(type_decl_f2);
  EXPECT_EQ(type_decl_f2->members.size(), 2);
  auto type_decl_f3 = library.LookupTable("F3");
  ASSERT_NOT_NULL(type_decl_f3);
  EXPECT_EQ(type_decl_f3->members.size(), 1);
  auto type_decl_f4 = library.LookupUnion("F4");
  ASSERT_NOT_NULL(type_decl_f4);
  EXPECT_EQ(type_decl_f4->members.size(), 1);
}

TEST(NewSyntaxTests, BadTypeDeclOfNewTypeErrors) {
  TestLibrary library(R"FIDL(
library example;
type S = struct{};
type N = S;
)FIDL");
  // allow_new_types is disabled, hence this should fail.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNewTypesNotAllowed);
}

TEST(NewSyntaxTests, GoodTypeParameters) {
  TestLibrary library(R"FIDL(
library example;
type Inner = struct{};
alias Alias = Inner;

type TypeDecl = struct {
  // vector of primitive
  v0 vector<uint8>;
  // vector of sourced
  v1 vector<Inner>;
  // vector of alias
  v2 vector<Alias>;
  // vector of anonymous layout
  v3 vector<struct{
       i0 struct{};
       i1 vector<struct{}>;
     }>;
  // array of primitive
  a0 array<uint8,5>;
  // array of sourced
  a1 array<Inner,5>;
  // array of alias
  a2 array<Alias,5>;
  // array of anonymous layout
  a3 array<struct{
       i2 struct{};
       i3 array<struct{},5>;
     },5>;
};
)FIDL");

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 8);
  auto type_decl_vector_anon = library.LookupStruct("V3");
  ASSERT_NOT_NULL(type_decl_vector_anon);
  EXPECT_EQ(type_decl_vector_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("I0"));
  ASSERT_NOT_NULL(library.LookupStruct("I1"));
  auto type_decl_array_anon = library.LookupStruct("A3");
  ASSERT_NOT_NULL(type_decl_array_anon);
  EXPECT_EQ(type_decl_array_anon->members.size(), 2);
  ASSERT_NOT_NULL(library.LookupStruct("I2"));
  ASSERT_NOT_NULL(library.LookupStruct("I3"));
}

TEST(NewSyntaxTests, GoodLayoutMemberConstraints) {
  TestLibrary library(R"FIDL(
library example;

alias Alias = vector<uint8>;
type t1 = resource struct {
  u0 union { 1: b bool; };
  u1 union { 1: b bool; }:optional;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto type_decl = library.LookupStruct("t1");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 2);

  size_t i = 0;

  auto u0_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(u0_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  auto u0_type = static_cast<const fidl::flat::IdentifierType*>(u0_type_base);
  EXPECT_EQ(u0_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(u0_type->type_decl->kind, fidl::flat::Decl::Kind::kUnion);

  auto u1_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(u1_type_base->kind, fidl::flat::Type::Kind::kIdentifier);
  auto u1_type = static_cast<const fidl::flat::IdentifierType*>(u1_type_base);
  EXPECT_EQ(u1_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(u1_type->type_decl->kind, fidl::flat::Decl::Kind::kUnion);
}

TEST(NewSyntaxTests, GoodConstraintsOnVectors) {
  TestLibrary library(R"FIDL(
library example;

alias Alias = vector<uint8>;
type TypeDecl= struct {
  v0 vector<bool>;
  v1 vector<bool>:16;
  v2 vector<bool>:optional;
  v3 vector<bool>:<16,optional>;
  b4 vector<uint8>;
  b5 vector<uint8>:16;
  b6 vector<uint8>:optional;
  b7 vector<uint8>:<16,optional>;
  s8 string;
  s9 string:16;
  s10 string:optional;
  s11 string:<16,optional>;
  a12 Alias;
  a13 Alias:16;
  a14 Alias:optional;
  a15 Alias:<16,optional>;
};
)FIDL");

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 16);

  size_t i = 0;

  auto v0_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(v0_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v0_type = static_cast<const fidl::flat::VectorType*>(v0_type_base);
  EXPECT_EQ(v0_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(v0_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v0_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto v1_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(v1_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v1_type = static_cast<const fidl::flat::VectorType*>(v1_type_base);
  EXPECT_EQ(v1_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(v1_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v1_type->element_count->value, 16u);

  auto v2_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(v2_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v2_type = static_cast<const fidl::flat::VectorType*>(v2_type_base);
  EXPECT_EQ(v2_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(v2_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(v2_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto v3_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(v3_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto v3_type = static_cast<const fidl::flat::VectorType*>(v3_type_base);
  EXPECT_EQ(v3_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(v3_type->element_count->value, 16u);

  auto b4_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(b4_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b4_type = static_cast<const fidl::flat::VectorType*>(b4_type_base);
  EXPECT_EQ(b4_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(b4_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto b5_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(b5_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b5_type = static_cast<const fidl::flat::VectorType*>(b5_type_base);
  EXPECT_EQ(b5_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(b5_type->element_count->value, 16u);

  auto b6_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(b6_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b6_type = static_cast<const fidl::flat::VectorType*>(b6_type_base);
  EXPECT_EQ(b6_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(b6_type->element_count, &fidl::flat::VectorType::kMaxSize);

  auto b7_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(b7_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto b7_type = static_cast<const fidl::flat::VectorType*>(b7_type_base);
  EXPECT_EQ(b7_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(b7_type->element_count->value, 16u);

  auto s8_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(s8_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s8_type = static_cast<const fidl::flat::StringType*>(s8_type_base);
  EXPECT_EQ(s8_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(s8_type->max_size, &fidl::flat::StringType::kMaxSize);

  auto s9_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(s9_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s9_type = static_cast<const fidl::flat::StringType*>(s9_type_base);
  EXPECT_EQ(s9_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(s9_type->max_size->value, 16u);

  auto s10_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(s10_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s10_type = static_cast<const fidl::flat::StringType*>(s10_type_base);
  EXPECT_EQ(s10_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(s10_type->max_size, &fidl::flat::StringType::kMaxSize);

  auto s11_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(s11_type_base->kind, fidl::flat::Type::Kind::kString);
  auto s11_type = static_cast<const fidl::flat::StringType*>(s11_type_base);
  EXPECT_EQ(s11_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(s11_type->max_size->value, 16u);

  auto a12_invocation = type_decl->members[i].type_ctor->resolved_params;
  EXPECT_NULL(a12_invocation.element_type_resolved);
  EXPECT_EQ(a12_invocation.nullability, fidl::types::Nullability::kNonnullable);
  auto a12_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(a12_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a12_type = static_cast<const fidl::flat::VectorType*>(a12_type_base);
  EXPECT_EQ(a12_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(a12_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a12_type->element_count, &fidl::flat::VectorType::kMaxSize);
  EXPECT_NULL(a12_invocation.size_resolved);

  auto a13_invocation = type_decl->members[i].type_ctor->resolved_params;
  EXPECT_NULL(a13_invocation.element_type_resolved);
  EXPECT_EQ(a13_invocation.nullability, fidl::types::Nullability::kNonnullable);
  auto a13_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(a13_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a13_type = static_cast<const fidl::flat::VectorType*>(a13_type_base);
  EXPECT_EQ(a13_type->nullability, fidl::types::Nullability::kNonnullable);
  EXPECT_EQ(a13_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a13_type->element_count->value, 16u);
  EXPECT_EQ(a13_type->element_count, a13_invocation.size_resolved);

  auto a14_invocation = type_decl->members[i].type_ctor->resolved_params;
  EXPECT_NULL(a14_invocation.element_type_resolved);
  EXPECT_EQ(a14_invocation.nullability, fidl::types::Nullability::kNullable);
  auto a14_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(a14_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a14_type = static_cast<const fidl::flat::VectorType*>(a14_type_base);
  EXPECT_EQ(a14_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(a14_type->element_type->kind, fidl::flat::Type::Kind::kPrimitive);
  EXPECT_EQ(a14_type->element_count, &fidl::flat::VectorType::kMaxSize);
  // EXPECT_EQ(a14_type->element_count, a14_invocation->maybe_size);
  EXPECT_NULL(a14_invocation.size_resolved);

  auto a15_invocation = type_decl->members[i].type_ctor->resolved_params;
  EXPECT_NULL(a15_invocation.element_type_resolved);
  EXPECT_EQ(a15_invocation.nullability, fidl::types::Nullability::kNullable);
  auto a15_type_base = type_decl->members[i++].type_ctor->type;
  ASSERT_EQ(a15_type_base->kind, fidl::flat::Type::Kind::kVector);
  auto a15_type = static_cast<const fidl::flat::VectorType*>(a15_type_base);
  EXPECT_EQ(a15_type->nullability, fidl::types::Nullability::kNullable);
  EXPECT_EQ(a15_type->element_count->value, 16u);
  EXPECT_EQ(a15_type->element_count, a15_invocation.size_resolved);
}

TEST(NewSyntaxTests, GoodConstraintsOnUnions) {
  TestLibrary library(R"FIDL(
library example;

type UnionDecl = union{1: foo bool;};
alias UnionAlias = UnionDecl;
type TypeDecl= struct {
  u0 union{1: bar bool;};
  u1 union{1: baz bool;}:optional;
  u2 UnionDecl;
  u3 UnionDecl:optional;
  u4 UnionAlias;
  u5 UnionAlias:optional;
};
)FIDL");

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);
  size_t i = 0;

  auto& u0 = type_decl->members[i++];
  auto u0_type = static_cast<const fidl::flat::IdentifierType*>(u0.type_ctor->type);
  EXPECT_EQ(u0_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u1 = type_decl->members[i++];
  auto u1_type = static_cast<const fidl::flat::IdentifierType*>(u1.type_ctor->type);
  EXPECT_EQ(u1_type->nullability, fidl::types::Nullability::kNullable);

  auto& u2 = type_decl->members[i++];
  auto u2_type = static_cast<const fidl::flat::IdentifierType*>(u2.type_ctor->type);
  EXPECT_EQ(u2_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u3 = type_decl->members[i++];
  auto u3_type = static_cast<const fidl::flat::IdentifierType*>(u3.type_ctor->type);
  EXPECT_EQ(u3_type->nullability, fidl::types::Nullability::kNullable);

  auto& u4 = type_decl->members[i++];
  auto u4_type = static_cast<const fidl::flat::IdentifierType*>(u4.type_ctor->type);
  EXPECT_EQ(u4_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& u5 = type_decl->members[i++];
  auto u5_type = static_cast<const fidl::flat::IdentifierType*>(u5.type_ctor->type);
  EXPECT_EQ(u5_type->nullability, fidl::types::Nullability::kNullable);
}

TEST(NewSyntaxTests, GoodConstraintsOnHandles) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type TypeDecl = resource struct {
  h0 zx.handle;
  h1 zx.handle:VMO;
  h2 zx.handle:optional;
  h3 zx.handle:<VMO,optional>;
  h4 zx.handle:<VMO,zx.rights.TRANSFER>;
  h5 zx.handle:<VMO,zx.rights.TRANSFER,optional>;
};
)FIDL");
  library.UseLibraryZx();

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupStruct("TypeDecl");
  ASSERT_NOT_NULL(type_decl);
  ASSERT_EQ(type_decl->members.size(), 6);

  auto& h0 = type_decl->members[0];
  auto h0_type = static_cast<const fidl::flat::HandleType*>(h0.type_ctor->type);
  EXPECT_EQ(h0_type->obj_type, 0u);
  EXPECT_EQ(h0_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h0_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h1 = type_decl->members[1];
  auto h1_type = static_cast<const fidl::flat::HandleType*>(h1.type_ctor->type);
  EXPECT_NE(h1_type->obj_type, 0u);
  EXPECT_EQ(h1_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h1_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h2 = type_decl->members[2];
  auto h2_type = static_cast<const fidl::flat::HandleType*>(h2.type_ctor->type);
  EXPECT_EQ(h2_type->obj_type, 0u);
  EXPECT_EQ(h2_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h2_type->nullability, fidl::types::Nullability::kNullable);

  auto& h3 = type_decl->members[3];
  auto h3_type = static_cast<const fidl::flat::HandleType*>(h3.type_ctor->type);
  EXPECT_EQ(h3_type->obj_type, 3u);  // VMO
  EXPECT_EQ(h3_type->rights, &fidl::flat::HandleType::kSameRights);
  EXPECT_EQ(h3_type->nullability, fidl::types::Nullability::kNullable);

  auto& h4 = type_decl->members[4];
  auto h4_type = static_cast<const fidl::flat::HandleType*>(h4.type_ctor->type);
  EXPECT_EQ(h4_type->obj_type, 3u);          // VMO
  EXPECT_EQ(h4_type->rights->value, 0x02u);  // TRANSFER
  EXPECT_EQ(h4_type->nullability, fidl::types::Nullability::kNonnullable);

  auto& h5 = type_decl->members[5];
  auto h5_type = static_cast<const fidl::flat::HandleType*>(h5.type_ctor->type);
  EXPECT_EQ(h5_type->obj_type, 3u);          // VMO
  EXPECT_EQ(h5_type->rights->value, 0x02u);  // TRANSFER
  EXPECT_EQ(h5_type->nullability, fidl::types::Nullability::kNullable);
}

TEST(NewSyntaxTests, BadTooManyLayoutParameters) {
  TestLibrary library;
  library.AddFile("bad/fi-0162-b.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(NewSyntaxTests, BadZeroParameters) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  foo array;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
  EXPECT_EQ(library.errors()[0]->span.data(), "array");
}

TEST(NewSyntaxTests, BadNotEnoughParameters) {
  TestLibrary library;
  library.AddFile("bad/fi-0162-a.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
  EXPECT_EQ(library.errors()[0]->span.data(), "<8>");
}

TEST(NewSyntaxTests, BadTooManyConstraints) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  foo uint8:<1, 2, 3>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
  EXPECT_EQ(library.errors()[0]->span.data(), "<1, 2, 3>");
}

TEST(NewSyntaxTests, BadParameterizedAnonymousLayout) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
  bar struct {}<1>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(NewSyntaxTests, BadConstrainTwice) {
  TestLibrary library(R"FIDL(
library example;

using zx;

alias MyVmo = zx.handle:VMO;

type Foo = struct {
    foo MyVmo:zx.obj_type.CHANNEL;
};

)FIDL");
  library.UseLibraryZx();

  // TODO(fxbug.dev/74193): We plan to disallow constraints on aliases, so this
  // error message should change to that. For now, to test this we have to use
  // `zx.obj_type` above because contextual lookup is not done through aliases.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotConstrainTwice);
}

TEST(NewSyntaxTests, GoodNoOverlappingConstraints) {
  TestLibrary library(R"FIDL(
library example;

using zx;

alias MyVmo = zx.handle:<VMO, zx.rights.TRANSFER>;

type Foo = resource struct {
    foo MyVmo:optional;
};

)FIDL");
  library.UseLibraryZx();

  ASSERT_COMPILED(library);
}

TEST(NewSyntaxTests, BadWantTypeLayoutParameter) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo vector<3>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedType);
}

TEST(NewSyntaxTests, BadWantValueLayoutParameter) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo array<uint8, uint8>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedValueButGotType);
}

TEST(NewSyntaxTests, BadShadowedOptional) {
  TestLibrary library(R"FIDL(
library example;

const optional uint8 = 3;

type Foo = resource struct {
    foo vector<uint8>:<10, optional>;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(NewSyntaxTests, BadWrongConstraintType) {
  TestLibrary library(R"FIDL(
library example;

type Foo = resource struct {
    foo vector<uint8>:"hello";
};
)FIDL");

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrUnexpectedConstraint);
}

TEST(InternalTypes, CannotReferToUnqualifiedInternalType) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo TransportErr;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(InternalTypes, CannotReferToQualifiedInternalType) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    foo fidl.TransportErr;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(TypesTests, BadUsizeWithoutFlag) {
  TestLibrary library;
  library.AddFile("bad/fi-0180.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExperimentalZxCTypesDisallowed);
}

TEST(TypesTests, BadExperimentalZxCTypesWithoutFlag) {
  for (std::string type : {"usize", "uintptr", "uchar", "experimental_pointer<uint32>"}) {
    TestLibrary library("library example; alias T = " + type + ";");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExperimentalZxCTypesDisallowed);
  }
}

TEST(TypesTests, GoodExperimentalZxCTypesWithFlag) {
  for (std::string type : {"usize", "uintptr", "uchar", "experimental_pointer<uint32>"}) {
    TestLibrary library("library example; alias T = " + type + ";");
    library.EnableFlag(fidl::ExperimentalFlags::Flag::kZxCTypes);
    ASSERT_COMPILED(library);
  }
}

}  // namespace fidl::flat
