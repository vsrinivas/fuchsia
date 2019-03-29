// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

namespace {

class ResolvePtrRefTest : public TestWithLoop {};

}  // namespace

TEST_F(ResolvePtrRefTest, NotPointer) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  auto int32_type = MakeInt32Type();
  ExprValue int32_value(int32_type, {0x00, 0x00, 0x00, 0x00});

  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolvePointer(
      provider, int32_value,
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  // This should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ("Attempting to dereference 'int32_t' which is not a pointer.",
            out_err.msg());

  // Pointer with incorrectly sized data.
  auto int32_ptr_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(int32_type));
  ExprValue int32_ptr_value(int32_ptr_type, {0x00, 0x00, 0x00, 0x00});

  called = false;
  ResolvePointer(
      provider, int32_ptr_value,
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  // This should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ(
      "The value of type 'int32_t*' is the incorrect size (expecting 8, got "
      "4). Please file a bug.",
      out_err.msg());
}

TEST_F(ResolvePtrRefTest, InvalidMemory) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  constexpr uint64_t kAddress = 0x10;

  auto int32_type = MakeInt32Type();

  // This read will return no data.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolvePointer(
      provider, kAddress, int32_type,
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_EQ("Invalid pointer 0x10", out_err.msg());

  // This read will return only 2 bytes (it requires 4).
  provider->AddMemory(kAddress, {0x00, 0x00});
  called = false;
  out_err = Err();
  ResolvePointer(
      provider, kAddress, int32_type,
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_EQ("Invalid pointer 0x10", out_err.msg());
}

// Tests EnsureResolveReference when the value is not a reference.
TEST_F(ResolvePtrRefTest, NotRef) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto int32_type = MakeInt32Type();

  ExprValue value(123);

  bool called = false;
  ExprValue out_value;
  EnsureResolveReference(
      provider, value, [&called, &out_value](const Err& err, ExprValue result) {
        EXPECT_FALSE(err.has_error());
        called = true;
        out_value = result;
      });

  // Should have run synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ(value, out_value);
}

// Tests EnsureResolveReference when the value is a const ref. The const should
// get ignored, the ref should be stripped, and the pointed-to value should be
// the result.
TEST_F(ResolvePtrRefTest, ConstRef) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // Add a 32-bit int at a given address.
  constexpr uint64_t kAddress = 0x300020;
  std::vector<uint8_t> int_value = {0x04, 0x03, 0x02, 0x01};
  provider->AddMemory(kAddress, int_value);

  // Make "volatile const int32_t&". This tests modifies on both sides of the
  // reference (volatile on the outside, const on the inside).
  auto int32_type = MakeInt32Type();
  auto const_int32_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kConstType, LazySymbol(int32_type));
  auto const_int32_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kReferenceType, LazySymbol(const_int32_type));
  auto volatile_const_int32_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kVolatileType, LazySymbol(const_int32_ref_type));

  ExprValue value(volatile_const_int32_ref_type,
                  {0x20, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00});

  bool called = false;
  ExprValue out_value;
  EnsureResolveReference(
      provider, value, [&called, &out_value](const Err& err, ExprValue result) {
        EXPECT_FALSE(err.has_error());
        called = true;
        out_value = result;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  // Should have run asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // The resolved type should be "const int32_t" and have the input value.
  EXPECT_EQ(const_int32_type.get(), out_value.type());
  EXPECT_EQ(int_value, out_value.data());

  // Check the location of the result.
  EXPECT_EQ(ExprValueSource::Type::kMemory, out_value.source().type());
  EXPECT_EQ(kAddress, out_value.source().address());
}

}  // namespace zxdb
