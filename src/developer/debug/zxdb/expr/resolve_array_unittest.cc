// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_array.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ResolveArrayTest : public TestWithLoop {};

// A PrettyType implementation that provides arracy access. This array access returns the index * 2
// as the array value.
class TestPrettyArray : public PrettyType {
 public:
  TestPrettyArray(){};

  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override {}
  EvalArrayFunction GetArrayAccess() const override {
    return [](fxl::RefPtr<EvalContext>, const ExprValue& object_value, int64_t index,
              fit::callback<void(const Err&, ExprValue)> cb) { cb(Err(), ExprValue(index * 2)); };
  }
};

}  // namespace

TEST_F(ResolveArrayTest, ResolveStatic) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Request 3 elements from 1-4.
  constexpr uint64_t kBaseAddress = 0x100000;
  constexpr uint32_t kBeginIndex = 1;
  constexpr uint32_t kEndIndex = 4;

  // Array holds 3 uint16_t.
  constexpr uint32_t kTypeSize = 2;
  auto elt_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, kTypeSize, "uint16_t");
  auto array_type = fxl::MakeRefCounted<ArrayType>(elt_type, 3);

  // Values are 0x1122, 0x3344, 0x5566
  std::vector<uint8_t> array_bytes = {0x22, 0x11, 0x44, 0x33, 0x66, 0x55};
  ExprValue value(array_type, array_bytes, ExprValueSource(kBaseAddress));

  std::vector<ExprValue> result;
  Err err = ResolveArray(eval_context, value, kBeginIndex, kEndIndex, &result);
  EXPECT_FALSE(err.has_error());

  // Should have returned two values (the overlap of the array and the
  // requested range).
  ASSERT_EQ(2u, result.size());

  EXPECT_EQ(elt_type.get(), result[0].type());
  EXPECT_EQ(0x3344, result[0].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize, result[0].source().address());

  EXPECT_EQ(elt_type.get(), result[1].type());
  EXPECT_EQ(0x5566, result[1].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize * 2, result[1].source().address());
}

// Resolves an array element with a pointer as the base.
TEST_F(ResolveArrayTest, ResolvePointer) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Request 3 elements from 1-4.
  constexpr uint64_t kBaseAddress = 0x100000;
  constexpr uint32_t kBeginIndex = 1;
  constexpr uint32_t kEndIndex = 4;

  // Array holds 3 uint16_t.
  constexpr uint32_t kTypeSize = 2;
  auto elt_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, kTypeSize, "uint16_t");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, elt_type);

  // Create memory with two values 0x3344, 0x5566. Note that these are offset one value from the
  // beginning of the array so the requested address of the kBeginIndex'th element matches this
  // address.
  constexpr uint64_t kBeginAddress = kBaseAddress + kBeginIndex * kTypeSize;
  eval_context->data_provider()->AddMemory(kBeginAddress, {0x44, 0x33, 0x66, 0x55});

  // Data in the value is the pointer to the beginning of the array.
  ExprValue value(ptr_type, {0, 0, 0x10, 0, 0, 0, 0, 0});

  bool called = false;
  Err out_err;
  std::vector<ExprValue> result;
  ResolveArray(eval_context, value, kBeginIndex, kEndIndex,
               [&called, &out_err, &result](const Err& err, std::vector<ExprValue> values) {
                 called = true;
                 out_err = err;
                 result = std::move(values);
                 debug_ipc::MessageLoop::Current()->QuitNow();
               });

  // Should be called async.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // Should have returned two values (the overlap of the array and the requested range).
  ASSERT_EQ(2u, result.size());

  EXPECT_EQ(elt_type.get(), result[0].type());
  EXPECT_EQ(0x3344, result[0].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize, result[0].source().address());

  EXPECT_EQ(elt_type.get(), result[1].type());
  EXPECT_EQ(0x5566, result[1].GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize * 2, result[1].source().address());

  // Test the one-element variant.
  called = false;
  out_err = Err();
  ExprValue single_result;
  ResolveArrayItem(eval_context, value, kBeginIndex,
                   [&called, &out_err, &single_result](const Err& err, ExprValue value) {
                     called = true;
                     out_err = err;
                     single_result = std::move(value);
                     debug_ipc::MessageLoop::Current()->QuitNow();
                   });

  // Should be called async.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  EXPECT_EQ(elt_type.get(), single_result.type());
  EXPECT_EQ(0x3344, single_result.GetAs<uint16_t>());
  EXPECT_EQ(kBaseAddress + kTypeSize, single_result.source().address());
}

TEST_F(ResolveArrayTest, Invalid) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Resolving an array on an empty ExprValue.
  bool called = false;
  ResolveArrayItem(eval_context, ExprValue(), 1,
               [&called](const Err& err, ExprValue values) {
                 called = true;
                 EXPECT_TRUE(err.has_error());
                 EXPECT_EQ("No type information.", err.msg());
               });
  EXPECT_TRUE(called);

  // Resolving an array on an integer type.
  called = false;
  ResolveArrayItem(eval_context, ExprValue(56), 1,
               [&called](const Err& err, ExprValue values) {
                 called = true;
                 EXPECT_TRUE(err.has_error());
                 EXPECT_EQ("Can't resolve an array access on type 'int32_t'.", err.msg());
               });
  EXPECT_TRUE(called);
}

// Tests a PrettyType's implementation of [].
TEST_F(ResolveArrayTest, PrettyArray) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  const char kMyTypeName[] = "MyType";

  // Set up pretty array mock for "MyType".
  TypeGlob mytype_glob;
  ASSERT_FALSE(mytype_glob.Init(kMyTypeName).has_error());
  eval_context->pretty_type_manager().Add(ExprLanguage::kC, mytype_glob,
                                          std::make_unique<TestPrettyArray>());

  auto my_type = MakeCollectionType(DwarfTag::kStructureType, kMyTypeName, {});
  ExprValue my_value(my_type, {});

  constexpr uint64_t kIndex = 55;

  // Test the one-element variant.
  bool called = false;
  Err out_err;
  ExprValue result;
  ResolveArrayItem(eval_context, my_value, kIndex,
                   [&called, &out_err, &result](const Err& err, ExprValue value) {
                     called = true;
                     out_err = err;
                     result = std::move(value);
                     debug_ipc::MessageLoop::Current()->QuitNow();
                   });

  // The PrettyType executes synchronously so it should complete synchronouly.
  EXPECT_TRUE(called);

  // Result should be twice the input.
  EXPECT_EQ(kIndex * 2, result.GetAs<uint64_t>());
}

}  // namespace zxdb
