// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/bitfield.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/eval_operators.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class Bitfield : public TestWithLoop {
 protected:
  ErrOrValue SyncEvalBinaryOperator(const fxl::RefPtr<EvalContext>& context, const ExprValue& left,
                                    ExprTokenType op, const ExprValue& right) {
    ErrOrValue result((ExprValue()));
    EvalBinaryOperator(context, left, ExprToken(op, "", 0), right,
                       [&result](ErrOrValue value) { result = value; });
    loop().RunUntilNoTasks();
    return result;
  }
};

}  // namespace

TEST_F(Bitfield, Bitfield) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Defines struct members that look like this:
  //   #pragma pack(push, 1)
  //   struct MyStruct {
  //     bool b1 : 1;
  //     bool b2 : 1;
  //     int i;  // To force things into other bytes.
  //     long long j : 3;
  //     unsigned k : 17;
  //   };
  // The values here are from copying the DWARF that the compiler generated from this struct.
  auto bool_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool");
  auto b1 = fxl::MakeRefCounted<DataMember>("b1", bool_type, 0);
  b1->set_byte_size(1);
  b1->set_bit_size(1);
  b1->set_bit_offset(7);

  auto b2 = fxl::MakeRefCounted<DataMember>("b2", bool_type, 0);
  b2->set_byte_size(1);
  b2->set_bit_size(1);
  b2->set_bit_offset(6);

  auto int_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int");
  auto i = fxl::MakeRefCounted<DataMember>("i", int_type, 1);

  // Note that the data member offset here is 0 which is weird, but the bit position works out
  // so it follows the integer.
  auto long_long_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "long long");
  auto j = fxl::MakeRefCounted<DataMember>("j", long_long_type, 0);
  j->set_byte_size(8);
  j->set_bit_size(3);
  j->set_bit_offset(21);

  auto unsigned_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "unsigned");
  auto k = fxl::MakeRefCounted<DataMember>("k", unsigned_type, 4);
  k->set_byte_size(4);
  k->set_bit_size(17);
  k->set_bit_offset(4);

  auto test_class_type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "MyStruct");
  test_class_type->set_byte_size(8);
  test_class_type->set_data_members(std::vector<LazySymbol>({b1, b2, i, j, k}));

  constexpr uint64_t kAddress = 0x102030;
  ExprValue all_zero(test_class_type, {0, 0, 0, 0, 0, 0, 0, 0}, ExprValueSource(kAddress));

  // Validate each one for the zero case. Note that the size we get out should be the size of
  // the variable it was declared with (not the bitfield size).
  auto out =
      ResolveBitfieldMember(eval_context, all_zero, FoundMember(test_class_type.get(), b1.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(false), out.value());
  EXPECT_EQ(ExprValueSource(kAddress, 1, 0), out.value().source());

  out = ResolveBitfieldMember(eval_context, all_zero, FoundMember(test_class_type.get(), b2.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(false), out.value());
  EXPECT_EQ(ExprValueSource(kAddress, 1, 1), out.value().source());

  out = ResolveBitfieldMember(eval_context, all_zero, FoundMember(test_class_type.get(), j.get()));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(ExprValue(static_cast<int64_t>(0)), out.value());
  EXPECT_EQ(ExprValueSource(kAddress, 3, 40), out.value().source());

  out = ResolveBitfieldMember(eval_context, all_zero, FoundMember(test_class_type.get(), k.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(0)), out.value());
  EXPECT_EQ(ExprValueSource(kAddress + 4, 17, 11), out.value().source());

  // Set bits to one one-at-a-time to make sure we find the right thing.
  out = ResolveBitfieldMember(eval_context, ExprValue(test_class_type, {1, 0, 0, 0, 0, 0, 0, 0}),
                              FoundMember(test_class_type.get(), b1.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(true), out.value());

  out = ResolveBitfieldMember(eval_context, ExprValue(test_class_type, {2, 0, 0, 0, 0, 0, 0, 0}),
                              FoundMember(test_class_type.get(), b2.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(true), out.value());

  // This one gets sign-extended because all bits are set.
  out = ResolveBitfieldMember(eval_context, ExprValue(test_class_type, {0, 0, 0, 0, 0, 7, 0, 0}),
                              FoundMember(test_class_type.get(), j.get()));
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(static_cast<int64_t>(-1)), out.value());

  ExprValue saturated_k(test_class_type, {0, 0, 0, 0, 0, 0xf8, 0xff, 0x0f});
  out =
      ResolveBitfieldMember(eval_context, saturated_k, FoundMember(test_class_type.get(), k.get()));
  ASSERT_FALSE(out.has_error());
  constexpr uint32_t kSaturatedK = 0x1ffff;
  EXPECT_EQ(ExprValue(kSaturatedK), out.value());

  // Test that resolving bitfields by pointer is hooked up.
  auto test_class_ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, test_class_type);
  eval_context->data_provider()->AddMemory(kAddress, saturated_k.data().bytes());
  ExprValue ptr_value(kAddress, test_class_ptr_type);

  bool called = false;
  ResolveMemberByPointer(eval_context, ptr_value, FoundMember(test_class_type.get(), k.get()),
                         [&called, &out](ErrOrValue value) {
                           called = true;
                           out = std::move(value);
                         });

  // Requesting the memory for the pointer is async.
  EXPECT_FALSE(called);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);

  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(ExprValue(kSaturatedK), out.value());
}

// Tests two cases: bitfields on a base class with an offset, and bitfields sprad across more
// bytes than the type of the bitfield.
TEST_F(Bitfield, NegativeBitOffset) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Defines struct members that look like this:
  //   // This just pushes the offset of Base members within the derived struct over by 4 bytes.
  //   struct SomeOtherBase {
  //     uint32_t value;
  //   }
  //
  //   #pragma pack(push, 1)
  //   struct Base {
  //     uint64_t b1 : 4;
  //     uint64_t b2 : 63;
  //   };
  //
  //   struct Derived : public SomeOtherBase, public Base {
  //   };
  //
  // The values here are from copying the DWARF that the compiler generated from this struct.

  auto uint64_t_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "uint64_t");
  const char kB1Name[] = "b1";
  auto b1 = fxl::MakeRefCounted<DataMember>(kB1Name, uint64_t_type, 0);
  b1->set_byte_size(8);
  b1->set_bit_size(4);
  b1->set_bit_offset(0x3c);

  const char kB2Name[] = "b2";
  auto b2 = fxl::MakeRefCounted<DataMember>(kB2Name, uint64_t_type, 0);
  b2->set_byte_size(8);
  b2->set_bit_size(0x3f);
  b2->set_bit_offset(-3);

  auto base_type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "Base");
  base_type->set_byte_size(9);
  base_type->set_data_members(std::vector<LazySymbol>({b1, b2}));

  auto derived_type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "Derived");
  constexpr uint64_t kBaseInDerived = 4u;  // Offset of Base in Derived.
  auto inherited = fxl::MakeRefCounted<InheritedFrom>(base_type, kBaseInDerived);
  derived_type->set_inherited_from(std::vector<LazySymbol>{inherited});
  derived_type->set_byte_size(kBaseInDerived + base_type->byte_size());

  constexpr uint64_t kAddress = 0x102030;
  ExprValue all_zero(derived_type, std::vector<uint8_t>(derived_type->byte_size()),
                     ExprValueSource(kAddress));

  // Read 0 from both members. This uses ResolveNonstaticMember which forces it to do the name
  // lookup and compute the offsets of the base member within the derived class.
  ErrOrValue out = ResolveNonstaticMember(eval_context, all_zero, ParsedIdentifier(kB1Name));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(0)), out.value());
  out = ResolveNonstaticMember(eval_context, all_zero, ParsedIdentifier(kB2Name));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(0)), out.value());

  // b1 = 3;
  // b2 = 123456;                 [padding-]
  ExprValue values(derived_type, {0, 0, 0, 0, 0x03, 0x24, 0x1e, 0, 0, 0, 0, 0, 0},
                   ExprValueSource(kAddress));
  out = ResolveNonstaticMember(eval_context, values, ParsedIdentifier(kB1Name));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(3)), out.value());
  out = ResolveNonstaticMember(eval_context, values, ParsedIdentifier(kB2Name));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(123456)), out.value());
}

// This goes through the general EvalBinaryOperator path to make sure the bitfield code is hooked up
// properly.
TEST_F(Bitfield, Assignment) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  auto int32_type = MakeInt32Type();

  constexpr uint64_t kAddress = 0x98723461923;
  constexpr uint32_t kBitSize = 3;
  constexpr uint32_t kBitShift = 2;
  ExprValue dest(int32_type, {0, 0, 0, 0}, ExprValueSource(kAddress, kBitSize, kBitShift));

  constexpr uint8_t kValue = 0b111;
  ExprValue all_ones(int32_type, {kValue, 0, 0, 0});

  // We haven't set the backing memory yet so the read will fail and the write will be skipped.
  ErrOrValue out = SyncEvalBinaryOperator(eval_context, dest, ExprTokenType::kEquals, all_ones);
  EXPECT_TRUE(out.has_error());
  auto mem_writes = eval_context->data_provider()->GetMemoryWrites();
  ASSERT_EQ(0u, mem_writes.size());

  // Provide all 0's backing memory and do the write again. This should succeed.
  eval_context->data_provider()->AddMemory(kAddress, {0, 0, 0, 0});
  out = SyncEvalBinaryOperator(eval_context, dest, ExprTokenType::kEquals, all_ones);
  ASSERT_FALSE(out.has_error());

  // Validate the 1's were written.
  mem_writes = eval_context->data_provider()->GetMemoryWrites();
  ASSERT_EQ(1u, mem_writes.size());
  EXPECT_EQ(kAddress, mem_writes[0].first);
  EXPECT_EQ(std::vector<uint8_t>({0b00011100}), mem_writes[0].second);

  // Now set the backing data to all 1's and write 0's.
  eval_context->data_provider()->AddMemory(kAddress, {0xff, 0xff, 0xff, 0xff});
  ExprValue all_zeroes(int32_type, {0, 0, 0, 0});
  out = SyncEvalBinaryOperator(eval_context, dest, ExprTokenType::kEquals, all_zeroes);
  ASSERT_FALSE(out.has_error());

  mem_writes = eval_context->data_provider()->GetMemoryWrites();
  ASSERT_EQ(1u, mem_writes.size());
  EXPECT_EQ(kAddress, mem_writes[0].first);
  EXPECT_EQ(std::vector<uint8_t>({0b11100011}), mem_writes[0].second);
}

}  // namespace zxdb
