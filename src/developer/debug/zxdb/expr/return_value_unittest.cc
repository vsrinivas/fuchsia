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

using debug_ipc::RegisterID;

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

  // Returning a 1-byte bool.
  context->data_provider()->AddRegisterValue(RegisterID::kX64_rax, true, 1);
  auto bool_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeBoolean, 1);
  result = GetReturnValueSync(context, bool_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(1u, result.value().GetAs<uint8_t>());

  // Provide a floating-point return register value.
  constexpr double kDoubleValue = 3.14;
  std::vector<uint8_t> float_data(sizeof(kDoubleValue));
  memcpy(float_data.data(), &kDoubleValue, sizeof(kDoubleValue));
  context->data_provider()->AddRegisterValue(RegisterID::kX64_xmm0, false, float_data);

  // Returning an 8-byte double.
  auto double_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeFloat, 8);
  result = GetReturnValueSync(context, double_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(kDoubleValue, result.value().GetAs<double>());

  // Returning a 4-byte float. This is actually returned as a double so it would need to be
  // internally casted.
  auto float_fn = MakeFunctionReturningBaseType(BaseType::kBaseTypeFloat, 4);
  result = GetReturnValueSync(context, float_fn.get());
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(static_cast<float>(kDoubleValue), result.value().GetAs<float>());
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
  EXPECT_EQ(data, result.value().data());
  EXPECT_EQ(kAddress, result.value().source().address());
}

}  // namespace zxdb
