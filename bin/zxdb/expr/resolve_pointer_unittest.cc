// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_pointer.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/common/test_with_loop.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class ResolvePointerTest : public TestWithLoop {};

}  // namespace

TEST_F(ResolvePointerTest, NotPointer) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  auto int32_type = MakeInt32Type();
  ExprValue int32_value(int32_type, {0x00, 0x00, 0x00, 0x00});

  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolvePointer(provider, int32_value, [&called, &out_err, &out_value](
                                            const Err& err, ExprValue value) {
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
      Symbol::kTagPointerType, LazySymbol(int32_type));
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

TEST_F(ResolvePointerTest, InvalidMemory) {
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

}  // namespace zxdb
