// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/return_value.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/abi_arm64.h"
#include "src/developer/debug/zxdb/expr/abi_x64.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

using debug::RegisterID;

// Returns a new vector consisting of the first |len| bytes of the given vector.
std::vector<uint8_t> DataPrefix(const std::vector<uint8_t>& source, size_t len) {
  return std::vector<uint8_t>(source.begin(), source.begin() + len);
}

class ReturnValue : public TestWithLoop {
 public:
  ErrOrValue GetReturnValueSync(const fxl::RefPtr<EvalContext>& context, const Function* func) {
    std::optional<ErrOrValue> result;
    GetReturnValue(context, func, [&result](ErrOrValue val) { result = std::move(val); });
    loop().RunUntilNoTasks();

    EXPECT_TRUE(result);  // Should always get the callback issued.
    if (!result)
      return Err("No result");
    return *result;
  }

  void TestStructureByValue(const fxl::RefPtr<EvalContext>& context, const std::string& name,
                            std::initializer_list<NameAndType> members,
                            std::vector<uint8_t> expected_contents) {
    auto coll = MakeCollectionType(DwarfTag::kStructureType, name, members);
    coll->set_calling_convention(Collection::kPassByValue);

    auto fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
    fn->set_return_type(coll);

    ErrOrValue result = GetReturnValueSync(context, fn.get());
    EXPECT_TRUE(result.ok()) << "Error evaluating return value for " << name;
    if (!result.ok())
      return;

    EXPECT_EQ(name, result.value().type()->GetFullName()) << " for " << name;
    EXPECT_EQ(expected_contents, result.value().data().bytes()) << " for " << name;
  }

  // Ensures that we can't compute the given structure's return-by-value value.
  void TestStructureByValueFails(const fxl::RefPtr<EvalContext>& context, const std::string& name,
                                 std::initializer_list<NameAndType> members) {
    auto coll = MakeCollectionType(DwarfTag::kStructureType, name, members);
    coll->set_calling_convention(Collection::kPassByValue);

    auto fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
    fn->set_return_type(coll);

    ErrOrValue result = GetReturnValueSync(context, fn.get());
    EXPECT_FALSE(result.ok()) << "Expecting failure for " << name;
  }
};

fxl::RefPtr<Function> MakeFunctionReturningBaseType(int base_type, uint32_t byte_size) {
  auto base = fxl::MakeRefCounted<BaseType>(base_type, byte_size, "ReturnType");

  auto fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  fn->set_return_type(base);

  return fn;
}

}  // namespace

TEST_F(ReturnValue, BaseTypeX64) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->set_abi(std::make_shared<AbiX64>());

  // Integer return value.
  constexpr uint64_t kIntValue = 42;
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, kIntValue);

  // Returning an 8-byte integer.
  auto int_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeSigned, 8);
  ErrOrValue result = GetReturnValueSync(context, int_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kIntValue, result.value().GetAs<uint64_t>());

  // Returning a 1-byte bool. The mock data provider doesn't know how to extract "al" from "rax"
  // so we have to supply a mapping for that explicitly.
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, 1);
  auto bool_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeBoolean, 1);
  result = GetReturnValueSync(context, bool_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(1u, result.value().GetAs<uint8_t>());

  // Provide a floating-point return register value. The xmm0 register holds two doubles but we
  // only use the first one.
  constexpr double kDoubleValue = 3.14;
  std::vector<uint8_t> double_data(16);  // 128-bit register.
  memcpy(double_data.data(), &kDoubleValue, sizeof(kDoubleValue));
  context->data_provider()->AddRegisterValue(RegisterID::kX64_xmm0, false, double_data);

  // Returning an 8-byte double.
  auto double_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeFloat, 8);
  result = GetReturnValueSync(context, double_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kDoubleValue, result.value().GetAs<double>());

  // Provide a 4-byte float in the low bits of xmm0.
  constexpr float kFloatValue = 2.17;
  std::vector<uint8_t> float_data(16);  // 128-bit register.
  memcpy(float_data.data(), &kFloatValue, sizeof(kFloatValue));
  context->data_provider()->AddRegisterValue(RegisterID::kX64_xmm0, false, float_data);

  // Returning a 4-byte float. This returns the low 4 bytes of xmm0.
  auto float_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeFloat, 4);
  result = GetReturnValueSync(context, float_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kFloatValue, result.value().GetAs<float>());
}

TEST_F(ReturnValue, BaseTypeARM64) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->set_abi(std::make_shared<AbiArm64>());

  // Integer return value.
  constexpr uint64_t kIntValue = 42;
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x0, true, kIntValue);

  // Returning an 8-byte integer.
  auto int_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeSigned, 8);
  ErrOrValue result = GetReturnValueSync(context, int_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kIntValue, result.value().GetAs<uint64_t>());

  // Returning a 1-byte bool.
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x0, true, 1);
  auto bool_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeBoolean, 1);
  result = GetReturnValueSync(context, bool_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(1u, result.value().GetAs<uint8_t>());

  // Provide a floating-point return register value. The v0 register holds two doubles but we
  // only fill in the first.
  constexpr double kDoubleValue = 3.14;
  std::vector<uint8_t> vector_data(16);  // 128-bit register.
  memcpy(vector_data.data(), &kDoubleValue, sizeof(kDoubleValue));
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_v0, false, vector_data);

  // Returning an 8-byte double.
  auto double_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeFloat, 8);
  result = GetReturnValueSync(context, double_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kDoubleValue, result.value().GetAs<double>());
}

TEST_F(ReturnValue, Pointer) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Make a function that returns "const int32_t*".
  auto int32_type = MakeInt32Type();
  auto const_int32_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, int32_type);
  auto const_int32_ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, const_int32_type);
  auto fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  fn->set_return_type(const_int32_ptr_type);

  // Set up x64 values.
  context->set_abi(std::make_shared<AbiX64>());
  constexpr uint64_t kPtrValue = 0x123456789abc;
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, kPtrValue);

  auto result = GetReturnValueSync(context, fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kPtrValue, result.value().GetAs<uint64_t>());
  EXPECT_EQ("const int32_t*", result.value().type()->GetFullName());

  // Set up ARM64 values.
  context->set_abi(std::make_shared<AbiArm64>());
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, 99);  // Poison old x86.
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x0, true, kPtrValue);

  result = GetReturnValueSync(context, fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kPtrValue, result.value().GetAs<uint64_t>());
  EXPECT_EQ("const int32_t*", result.value().type()->GetFullName());
}

TEST_F(ReturnValue, Enum) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Old-style untyped enumeration type.
  Enumeration::Map map;
  map[0] = "kZero";
  map[1] = "kOne";
  map[2] = "kTwo";
  auto untyped_enum = fxl::MakeRefCounted<Enumeration>("MyEnum", LazySymbol(), 4, true, map);
  auto untyped_fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  untyped_fn->set_return_type(untyped_enum);

  // New-style C++ typed enumeration (use int16).
  auto int16_type = MakeInt16Type();
  auto typed_enum = fxl::MakeRefCounted<Enumeration>("MyEnum", int16_type, 2, true, map);
  auto typed_fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  typed_fn->set_return_type(typed_enum);

  // Set up calls for x64 tests.
  constexpr uint64_t kEnumValue = 2;
  context->set_abi(std::make_shared<AbiX64>());
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, kEnumValue);

  // Untyped x64 enumeration.
  auto result = GetReturnValueSync(context, untyped_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kEnumValue, result.value().GetAs<uint32_t>());  // Declared a 4-byte enum size.
  EXPECT_EQ("MyEnum", result.value().type()->GetFullName());

  // Typed x64 enumeration.
  result = GetReturnValueSync(context, typed_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kEnumValue, result.value().GetAs<uint16_t>());

  // Set up calls for ARM64.
  context->set_abi(std::make_shared<AbiArm64>());
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, 99);  // Poison old x86.
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x0, true, kEnumValue);

  // Untyped ARM64 enumeration.
  result = GetReturnValueSync(context, untyped_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kEnumValue, result.value().GetAs<uint32_t>());  // Declared a 4-byte enum size.

  // Typed ARM64 enumeration.
  result = GetReturnValueSync(context, typed_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kEnumValue, result.value().GetAs<uint16_t>());
}

TEST_F(ReturnValue, CollectionByRefX64) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->set_abi(std::make_shared<AbiX64>());

  // Make a pass-by-ref struct type.
  auto int64_type = MakeInt64Type();
  auto coll = MakeCollectionType(DwarfTag::kStructureType, "MyStruct",
                                 {{"a", int64_type}, {"b", int64_type}, {"c", int64_type}});
  coll->set_calling_convention(Collection::kPassByReference);

  // Struct location is returned in rax.
  constexpr uint64_t kAddress = 0x2384f576a230;
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, kAddress);

  // Struct data.
  std::vector<uint8_t> data{0x42, 0, 0, 0, 0, 0, 0, 0,   // a
                            0x99, 0, 0, 0, 0, 0, 0, 0,   // b
                            0xff, 0, 0, 0, 0, 0, 0, 0};  // c
  context->data_provider()->AddMemory(kAddress, data);

  auto fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  fn->set_return_type(coll);

  auto result = GetReturnValueSync(context, fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(data, result.value().data().bytes());
  EXPECT_EQ(kAddress, result.value().source().address());
}

TEST_F(ReturnValue, CollectionByValueX64) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->set_abi(std::make_shared<AbiX64>());

  auto int32_type = MakeInt32Type();
  auto int64_type = MakeInt64Type();
  auto float_type = MakeFloatType();
  auto char_type = MakeSignedChar8Type();
  auto char_pointer_type = MakeCharPointerType();
  auto double_type = MakeDoubleType();

  std::vector<uint8_t> rax_data{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
  std::vector<uint8_t> rdx_data{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, rax_data);
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rdx, true, rdx_data);

  std::vector<uint8_t> xmm0_data{0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};
  std::vector<uint8_t> xmm1_data{0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97};
  context->data_provider()->AddRegisterValue(RegisterID::kX64_xmm0, false, xmm0_data);
  context->data_provider()->AddRegisterValue(RegisterID::kX64_xmm1, false, xmm1_data);

  // Structures consisting of floating-point data should use xmm0 followed by xmm1 registers, 8
  // bytes each. Going beyond 16 bytes should fail. All purely floating-point outputs should be
  // prefixes of this data.
  std::vector<uint8_t> float_data;
  float_data.insert(float_data.end(), xmm0_data.begin(), xmm0_data.end());
  float_data.insert(float_data.end(), xmm1_data.begin(), xmm1_data.end());

  TestStructureByValue(context, "OneFloat", {{"f", float_type}}, DataPrefix(float_data, 4));
  TestStructureByValue(context, "TwoFloat", {{"f", float_type}, {"g", float_type}},
                       DataPrefix(float_data, 8));
  TestStructureByValue(context, "TwoFloatDouble",
                       {{"f", float_type}, {"g", float_type}, {"d", double_type}},
                       DataPrefix(float_data, 16));
  TestStructureByValueFails(
      context, "TwoFloatDoubleFloat",
      {{"f", float_type}, {"g", float_type}, {"d", double_type}, {"e", float_type}});

  // Structures consisting of only integer and pointer data should use rax followed by rdx.
  std::vector<uint8_t> int_data;
  int_data.insert(int_data.end(), rax_data.begin(), rax_data.end());
  int_data.insert(int_data.end(), rdx_data.begin(), rdx_data.end());

  TestStructureByValue(context, "OneChar", {{"c", char_type}}, DataPrefix(int_data, 1));
  TestStructureByValue(context, "Int64OneChar", {{"i", int64_type}, {"c", char_type}},
                       DataPrefix(int_data, 9));
  TestStructureByValue(context, "Int64TwoChar",
                       {{"i", int64_type}, {"c", char_type}, {"c2", char_type}},
                       DataPrefix(int_data, 10));
  TestStructureByValue(context, "Int64Pointer", {{"i", int64_type}, {"c", char_pointer_type}},
                       DataPrefix(int_data, 16));
  TestStructureByValueFails(context, "ThreeInt64",
                            {{"i", int64_type}, {"j", int64_type}, {"k", int64_type}});

  // Combining an integer and a float in the same 8-byte word will pack the result as an integer.
  TestStructureByValue(context, "FloatChar", {{"f", float_type}, {"c", char_type}},
                       DataPrefix(int_data, 5));

  // For structures returned in an integer and a floating-point register.
  std::vector<uint8_t> int_float_data;
  int_float_data.insert(int_float_data.end(), rax_data.begin(), rax_data.end());
  int_float_data.insert(int_float_data.end(), xmm0_data.begin(), xmm0_data.end());

  TestStructureByValue(context, "FloatInt32Double",
                       {{"f", float_type}, {"i", int32_type}, {"d", double_type}},
                       DataPrefix(int_float_data, 16));
}

TEST_F(ReturnValue, CollectionByValueARM64) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->set_abi(std::make_shared<AbiArm64>());

  auto int64_type = MakeInt64Type();
  auto float_type = MakeFloatType();
  auto char_type = MakeSignedChar8Type();
  auto char_pointer_type = MakeCharPointerType();
  auto double_type = MakeDoubleType();

  std::vector<uint8_t> x0_data{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
  std::vector<uint8_t> x1_data{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x0, true, x0_data);
  context->data_provider()->AddRegisterValue(RegisterID::kARMv8_x1, true, x1_data);

  // The data is just packed linrarly for all structures.
  std::vector<uint8_t> data;
  data.insert(data.end(), x0_data.begin(), x0_data.end());
  data.insert(data.end(), x1_data.begin(), x1_data.end());

  TestStructureByValue(context, "OneChar", {{"c", char_type}}, DataPrefix(data, 1));
  TestStructureByValue(context, "OneFloat", {{"f", float_type}}, DataPrefix(data, 4));
  TestStructureByValue(context, "TwoFloatDouble",
                       {{"f", float_type}, {"g", float_type}, {"d", double_type}},
                       DataPrefix(data, 16));
  TestStructureByValue(context, "Int64Pointer", {{"i", int64_type}, {"c", char_pointer_type}},
                       DataPrefix(data, 16));
  TestStructureByValueFails(
      context, "TwoFloatDoubleFloat",
      {{"f", float_type}, {"g", float_type}, {"d", double_type}, {"e", float_type}});
}

}  // namespace zxdb
