// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_symbol_factory.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_test_util.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/template_parameter.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

namespace {

const char kDoStructCallName[] = "DoStructCall";
const char kGetIntPtrName[] = "GetIntPtr";
const char kGetStructMemberPtrName[] = "GetStructMemberPtr";
const char kPassRValueRefName[] = "PassRValueRef";
const char kCallInlineMemberName[] = "CallInlineMember";
const char kCallInlineName[] = "CallInline";

// Returns the function symbol with the given name. The name is assumed to exit as this function
// will EXPECT_* it to be valid. Returns empty refptr on failure.
fxl::RefPtr<const Function> GetFunctionWithName(fxl::RefPtr<ModuleSymbolsImpl>& module_symbols,
                                                const std::string& name) {
  DwarfSymbolFactory* factory = module_symbols->symbol_factory();

  llvm::DWARFUnit* unit = GetUnitWithNameEndingIn(module_symbols->context(),
                                                  module_symbols->compile_units(), "/type_test.cc");
  EXPECT_TRUE(unit);
  if (!unit)
    return nullptr;

  // Find the GetIntPtr function.
  llvm::DWARFDie function_die = GetFirstDieOfTagAndName(module_symbols->context(), unit,
                                                        llvm::dwarf::DW_TAG_subprogram, name);
  EXPECT_TRUE(function_die);
  if (!function_die)
    return nullptr;

  // Should make a valid lazy reference to the function DIE.
  LazySymbol lazy_function = factory->MakeLazy(function_die);
  EXPECT_TRUE(lazy_function);
  if (!lazy_function)
    return nullptr;

  // Deserialize to a function object.
  const Symbol* function_symbol = lazy_function.Get();
  EXPECT_EQ(DwarfTag::kSubprogram, function_symbol->tag());
  return fxl::RefPtr<const Function>(function_symbol->AsFunction());
}

}  // namespace

TEST(DwarfSymbolFactory, Function) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetIntPtrName);
  ASSERT_TRUE(function);

  // Unmangled name.
  EXPECT_EQ(kGetIntPtrName, function->GetAssignedName());

  // Mangled name. This tries not to depend on the exact name mangling rules while validating that
  // it's reasonable. The mangled name shouldn't be exactly the same as the unmangled name, but
  // should at least contain it.
  EXPECT_NE(kGetIntPtrName, function->linkage_name());
  EXPECT_NE(std::string::npos, function->linkage_name().find(kGetIntPtrName));

  // Declaration location.
  EXPECT_TRUE(function->decl_line().is_valid());
  EXPECT_TRUE(StringEndsWith(function->decl_line().file(), "/type_test.cc"))
      << function->decl_line().file();
  EXPECT_EQ(15, function->decl_line().line());

  // Note: return type tested by ModifiedBaseType.
}

TEST(DwarfSymbolFactory, PtrToMemberFunction) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  fxl::RefPtr<const Function> get_function =
      GetFunctionWithName(module_symbols, kGetStructMemberPtrName);
  ASSERT_TRUE(get_function);

  // Get the return type, this is a typedef (because functions can't return pointers to member
  // functions).
  auto return_typedef = get_function->return_type().Get()->AsModifiedType();
  ASSERT_TRUE(return_typedef);

  // The typedef references the member pointer. The type name encapsulates all return values and
  // parameters so this tests everything at once.
  const Symbol* member = return_typedef->modified().Get();
  EXPECT_EQ("int (my_ns::Struct::*)(my_ns::Struct*, char)", member->GetFullName());
}

TEST(DwarfSymbolFactory, InlinedMemberFunction) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the CallInline function.
  fxl::RefPtr<const Function> call_function =
      GetFunctionWithName(module_symbols, kCallInlineMemberName);
  ASSERT_TRUE(call_function);

  // It should have one inner block that's the inline function.
  ASSERT_EQ(1u, call_function->inner_blocks().size());
  const Function* inline_func = call_function->inner_blocks()[0].Get()->AsFunction();
  ASSERT_TRUE(inline_func);
  EXPECT_EQ(DwarfTag::kInlinedSubroutine, inline_func->tag());

  EXPECT_EQ("ForInline::InlinedFunction", inline_func->GetFullName());

  // The inline function should have two parameters, "this" and "param".
  ASSERT_EQ(2u, inline_func->parameters().size());
  const Variable* this_param = inline_func->parameters()[0].Get()->AsVariable();
  ASSERT_TRUE(this_param);
  EXPECT_EQ("this", this_param->GetAssignedName());
  const Variable* param_param = inline_func->parameters()[1].Get()->AsVariable();
  ASSERT_TRUE(param_param);
  EXPECT_EQ("param", param_param->GetAssignedName());

  // The object pointer on the function should refer to the "this" pointer retrieved above. This is
  // tricky because normally the object pointer is on the "abstract origin" of the inlined routine,
  // and will refer to a "this" parameter specified on the abstract origin. We need to correlate it
  // to the one on the inlined instance to get the location correct.
  EXPECT_EQ(this_param, inline_func->GetObjectPointerVariable());
}

TEST(DwarfSymbolFactory, InlinedFunction) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the CallInline function.
  fxl::RefPtr<const Function> call_function = GetFunctionWithName(module_symbols, kCallInlineName);
  ASSERT_TRUE(call_function);

  // It should have one inner block that's the inline function.
  ASSERT_EQ(1u, call_function->inner_blocks().size());
  const Function* inline_func = call_function->inner_blocks()[0].Get()->AsFunction();
  ASSERT_TRUE(inline_func);
  EXPECT_EQ(DwarfTag::kInlinedSubroutine, inline_func->tag());

  // Parameter decoding is tested by the InlinedMemberFunction test above. Here we care that the
  // enclosing namespace is correct because of the different ways these inlined routines are
  // declared.
  EXPECT_EQ("my_ns::InlinedFunction", inline_func->GetFullName());

  // The containing block of the inline function should be the calling function. Note that the
  // objects may not be the same.
  ASSERT_TRUE(inline_func->containing_block());
  auto containing_func = inline_func->containing_block().Get()->AsFunction();
  ASSERT_TRUE(containing_func);
  EXPECT_EQ(kCallInlineName, containing_func->GetFullName());
}

TEST(DwarfSymbolFactory, ModifiedBaseType) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetIntPtrName);
  ASSERT_TRUE(function);

  // Get the return type, this references a "pointer" modifier.
  EXPECT_TRUE(function->return_type().is_valid());
  const ModifiedType* ptr_mod = function->return_type().Get()->AsModifiedType();
  ASSERT_TRUE(ptr_mod) << "Tag = " << static_cast<int>(function->return_type().Get()->tag());
  EXPECT_EQ(DwarfTag::kPointerType, ptr_mod->tag());
  EXPECT_EQ("const int*", ptr_mod->GetFullName());

  // The modified type should be a "const" modifier.
  const ModifiedType* const_mod = ptr_mod->modified().Get()->AsModifiedType();
  ASSERT_TRUE(const_mod) << "Tag = " << static_cast<int>(function->return_type().Get()->tag());
  EXPECT_EQ(DwarfTag::kConstType, const_mod->tag());
  EXPECT_EQ("const int", const_mod->GetFullName());

  // The modified type should be the int base type.
  const BaseType* base = const_mod->modified().Get()->AsBaseType();
  ASSERT_TRUE(base);
  EXPECT_EQ(DwarfTag::kBaseType, base->tag());
  EXPECT_EQ("int", base->GetFullName());

  // Validate the BaseType parameters.
  EXPECT_EQ(BaseType::kBaseTypeSigned, base->base_type());
  EXPECT_EQ("int", base->GetAssignedName());
  // Try to be flexible about the size of ints on the platform.
  EXPECT_TRUE(base->byte_size() == 4 || base->byte_size() == 8);
}

TEST(DwarfSymbolFactory, RValueRef) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kPassRValueRefName);
  ASSERT_TRUE(function);

  // Should have one parameter of rvalue ref type.
  ASSERT_EQ(1u, function->parameters().size());
  const Variable* var = function->parameters()[0].Get()->AsVariable();
  ASSERT_TRUE(var);
  const ModifiedType* modified = var->type().Get()->AsModifiedType();
  ASSERT_TRUE(modified);
  EXPECT_EQ(DwarfTag::kRvalueReferenceType, modified->tag());

  EXPECT_EQ("int&&", modified->GetFullName());
}

TEST(DwarfSymbolFactory, ArrayType) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetString function.
  const char kGetString[] = "GetString";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetString);
  ASSERT_TRUE(function);

  // Find the "str_array" variable in the function.
  ASSERT_EQ(1u, function->variables().size());
  const Variable* str_array = function->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(str_array);
  EXPECT_EQ("str_array", str_array->GetAssignedName());

  // It should be an array type with length 14.
  const ArrayType* array_type = str_array->type().Get()->AsArrayType();
  ASSERT_TRUE(array_type);
  EXPECT_EQ(14u, array_type->num_elts());
  EXPECT_EQ("const char[14]", array_type->GetFullName());

  // The inner type should be a "char".
  const Type* elt_type = array_type->value_type();
  ASSERT_TRUE(elt_type);
  EXPECT_EQ("const char", elt_type->GetFullName());
}

TEST(DwarfSymbolFactory, Array2D) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the My2DArray function.
  const char kMy2DArray[] = "My2DArray";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kMy2DArray);
  ASSERT_TRUE(function);

  // Find the "array" variable in the function. It's declared as:
  //
  //   int array[3][4]
  //
  ASSERT_EQ(1u, function->variables().size());
  const Variable* array = function->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(array);
  EXPECT_EQ("array", array->GetAssignedName());

  // It should be an array type with length 3 (outer dimension).
  const ArrayType* outer_array_type = array->type().Get()->AsArrayType();
  ASSERT_TRUE(outer_array_type);
  EXPECT_EQ(3u, outer_array_type->num_elts());
  EXPECT_EQ("int[3][4]", outer_array_type->GetFullName());

  // The inner array type should be a int[4].
  const Type* inner_type = outer_array_type->value_type();
  ASSERT_TRUE(inner_type);
  const ArrayType* inner_array_type = inner_type->AsArrayType();
  ASSERT_TRUE(inner_array_type);
  EXPECT_EQ(4u, inner_array_type->num_elts());
  EXPECT_EQ("int[4]", inner_array_type->GetFullName());

  // The final contained type type should be a "int".
  const Type* elt_type = inner_array_type->value_type();
  ASSERT_TRUE(elt_type);
  EXPECT_EQ("int", elt_type->GetFullName());
}

TEST(DwarfSymbolFactory, Collection) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetStruct function.
  const char kGetStruct[] = "GetStruct";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetStruct);
  ASSERT_TRUE(function);

  // The return type should be the struct.
  auto* struct_type = function->return_type().Get()->AsCollection();
  ASSERT_TRUE(struct_type);
  EXPECT_EQ("my_ns::Struct", struct_type->GetFullName());

  // The struct has five data members and two base classes.
  ASSERT_EQ(5u, struct_type->data_members().size());
  ASSERT_EQ(2u, struct_type->inherited_from().size());

  // The first thing should be Base1 at offset 0.
  auto* base1 = struct_type->inherited_from()[0].Get()->AsInheritedFrom();
  ASSERT_TRUE(base1);
  auto* base1_type = base1->from().Get()->AsType();
  EXPECT_EQ("my_ns::Base1", base1_type->GetFullName());
  EXPECT_EQ(InheritedFrom::kConstant, base1->kind());
  EXPECT_EQ(0u, base1->offset());

  // It should be followed by Base2. To allow flexibility in packing without breaking this test, all
  // offsets below check only that the offset is greater than the previous one and a multiple of 4.
  auto* base2 = struct_type->inherited_from()[1].Get()->AsInheritedFrom();
  ASSERT_TRUE(base2);
  auto* base2_type = base2->from().Get()->AsType();
  EXPECT_EQ("my_ns::Base2", base2_type->GetFullName());
  EXPECT_EQ(InheritedFrom::kConstant, base2->kind());
  EXPECT_LT(0u, base2->offset());
  EXPECT_TRUE(base2->offset() % 4 == 0);

  // The base classes should be followed by the data members on the struct.
  auto* member_a = struct_type->data_members()[0].Get()->AsDataMember();
  ASSERT_TRUE(member_a);
  auto* member_a_type = member_a->type().Get()->AsType();
  EXPECT_EQ("int", member_a_type->GetFullName());
  EXPECT_LT(base2->offset(), member_a->member_location());
  EXPECT_TRUE(member_a->member_location() % 4 == 0);

  // The second data member should be "Struct* member_b".
  auto* member_b = struct_type->data_members()[1].Get()->AsDataMember();
  ASSERT_TRUE(member_b);
  auto* member_b_type = member_b->type().Get()->AsType();
  EXPECT_EQ("my_ns::Struct*", member_b_type->GetFullName());
  EXPECT_LT(member_a->member_location(), member_b->member_location());
  EXPECT_TRUE(member_b->member_location() % 4 == 0);

  // The third data member is "const void* v". Void is weird because it will be represented as a
  // modified pointer type of nothing.
  auto* member_v = struct_type->data_members()[2].Get()->AsDataMember();
  ASSERT_TRUE(member_v);
  auto* member_v_type = member_v->type().Get()->AsType();
  EXPECT_EQ("const void*", member_v_type->GetFullName());
  EXPECT_LT(member_b->member_location(), member_v->member_location());
  EXPECT_TRUE(member_v->member_location() % 4 == 0);

  // The next data member should be kConstInt = -2. This is stored in a ConstValue as a
  // little-endian 64-bit signed value.
  //
  // This assumes the compiler has encoded the constexpr as a DW_AT_const_value. It's theoretically
  // possible for the constant value to be encoded as a DWARF expression but none of our compilers
  // currently do that and we really want to test ConstValue here.
  auto* member_ci = struct_type->data_members()[3].Get()->AsDataMember();
  ASSERT_TRUE(member_ci);
  EXPECT_TRUE(member_ci->is_external());
  EXPECT_EQ("const int", member_ci->type().Get()->AsType()->GetFullName());
  EXPECT_TRUE(member_ci->const_value().has_value());
  std::vector<uint8_t> expected_minus_two{0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  EXPECT_EQ(expected_minus_two, member_ci->const_value().GetConstValue(8));

  // kConstLongDouble (see kConstInt above for notes).
  auto* member_cd = struct_type->data_members()[4].Get()->AsDataMember();
  ASSERT_TRUE(member_cd);
  EXPECT_TRUE(member_cd->is_external());
  EXPECT_EQ("const long double", member_cd->type().Get()->AsType()->GetFullName());
  EXPECT_TRUE(member_cd->const_value().has_value());
  // This is a "long double" which on x86 is 80 bits, but on ARM is the same as a double. Accept
  // either encoding of "3.14". We want to test non-integer, > 64-bit values here if possible.
  std::vector<uint8_t> expected_80bit{0, 0xf8, 0x28, 0x5c, 0x8f, 0xc2, 0xf5, 0xc8, 0, 0x40};
  std::vector<uint8_t> expected_64bit{0x1f, 0x85, 0xeb, 0x51, 0xb8, 0x1e, 0x09, 0x40};
  EXPECT_TRUE(expected_64bit == member_cd->const_value().GetConstValue(8) ||
              expected_80bit == member_cd->const_value().GetConstValue(10));
}

// Covers cases of InheritedFrom not covered by the collection test above.
TEST(DwarfSymbolFactory, InheritedFrom) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  const char kGetVirtualDerived[] = "GetVirtualDerived";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetVirtualDerived);
  ASSERT_TRUE(function);

  auto* derived_type = function->return_type().Get()->AsCollection();
  ASSERT_TRUE(derived_type);
  EXPECT_EQ("VirtualDerived", derived_type->GetFullName());

  ASSERT_EQ(1u, derived_type->inherited_from().size());
  const InheritedFrom* inherited = derived_type->inherited_from()[0].Get()->AsInheritedFrom();
  ASSERT_TRUE(inherited);

  // Validate that it has a nonempty expression. This test doesn't require that the expression
  // be a specific thing.
  EXPECT_EQ(InheritedFrom::kExpression, inherited->kind());
  EXPECT_FALSE(inherited->location_expression().empty());
}

TEST(DwarfSymbolFactory, Enum) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetStruct function.
  const char kGetStruct[] = "GetStructWithEnums";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetStruct);
  ASSERT_TRUE(function);

  // The return type should be the struct.
  auto* struct_type = function->return_type().Get()->AsCollection();
  ASSERT_TRUE(struct_type);
  EXPECT_EQ("StructWithEnums", struct_type->GetFullName());

  // There are three enum members defined in the struct.
  ASSERT_EQ(3u, struct_type->data_members().size());

  // First is a regular enum with no values.
  auto regular_enum =
      struct_type->data_members()[0].Get()->AsDataMember()->type().Get()->AsEnumeration();
  ASSERT_TRUE(regular_enum);
  EXPECT_EQ("StructWithEnums::RegularEnum", regular_enum->GetFullName());
  EXPECT_TRUE(regular_enum->values().empty());

  // Second is an anonymous signed enum with two values. We don't bother to test the enumerator
  // values on this one since some aspects will be compiler-dependent.
  auto anon_enum =
      struct_type->data_members()[1].Get()->AsDataMember()->type().Get()->AsEnumeration();
  ASSERT_TRUE(anon_enum);
  EXPECT_EQ("StructWithEnums::(anon enum)", anon_enum->GetFullName());
  EXPECT_TRUE(anon_enum->is_signed());
  EXPECT_EQ(2u, anon_enum->values().size());

  // Third is a type enum with two values.
  auto typed_enum =
      struct_type->data_members()[2].Get()->AsDataMember()->type().Get()->AsEnumeration();
  ASSERT_TRUE(typed_enum);
  EXPECT_EQ("StructWithEnums::TypedEnum", typed_enum->GetFullName());
  EXPECT_TRUE(typed_enum->is_signed());
  ASSERT_EQ(2u, typed_enum->values().size());
  EXPECT_EQ(1u, typed_enum->byte_size());

  // Since this is typed, the values should be known. The map contains the signed values casted to
  // an unsigned int.
  auto first_value = *typed_enum->values().begin();
  auto second_value = *(++typed_enum->values().begin());
  EXPECT_EQ(1u, first_value.first);
  EXPECT_EQ("TYPED_B", first_value.second);
  EXPECT_EQ(static_cast<uint64_t>(-1), second_value.first);
  EXPECT_EQ("TYPED_A", second_value.second);
}

// Tests nested code blocks, variables, and parameters.
TEST(DwarfSymbolFactory, CodeBlocks) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the DoStructCall function.
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kDoStructCallName);
  ASSERT_TRUE(function);

  // It should have two parameters, arg1 and arg2.
  const Variable* struct_arg = nullptr;
  const Variable* int_arg = nullptr;
  ASSERT_EQ(2u, function->parameters().size());
  for (const auto& param : function->parameters()) {
    const Variable* cur_var = param.Get()->AsVariable();
    ASSERT_TRUE(cur_var);  // Each parameter should decode to a variable.
    if (cur_var->GetAssignedName() == "arg1")
      struct_arg = cur_var;
    else if (cur_var->GetAssignedName() == "arg2")
      int_arg = cur_var;
  }

  // Both args should have valid locations with non-empty expressions. This doesn't test the actual
  // programs because that could vary by build.
  ASSERT_FALSE(struct_arg->location().is_null());
  EXPECT_FALSE(struct_arg->location().locations()[0].expression.empty());
  ASSERT_FALSE(int_arg->location().is_null());
  EXPECT_FALSE(int_arg->location().locations()[0].expression.empty());

  // Validate the arg1 type (const Struct&).
  ASSERT_TRUE(struct_arg);
  const Type* struct_arg_type = struct_arg->type().Get()->AsType();
  ASSERT_TRUE(struct_arg_type);
  EXPECT_EQ("const my_ns::Struct&", struct_arg_type->GetFullName());

  // Validate the arg2 type (int).
  ASSERT_TRUE(int_arg);
  const Type* int_arg_type = int_arg->type().Get()->AsType();
  ASSERT_TRUE(int_arg_type);
  EXPECT_EQ("int", int_arg_type->GetFullName());

  // The function block should have one variable (var1).
  ASSERT_EQ(1u, function->variables().size());
  const Variable* var1 = function->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(var1);
  const Type* var1_type = var1->type().Get()->AsType();
  ASSERT_TRUE(var1_type);
  EXPECT_EQ("volatile int", var1_type->GetFullName());

  // There should be one child lexical scope.
  ASSERT_EQ(1u, function->inner_blocks().size());
  const CodeBlock* inner = function->inner_blocks()[0].Get()->AsCodeBlock();

  // The lexical scope should have one child variable.
  ASSERT_EQ(1u, inner->variables().size());
  const Variable* var2 = inner->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(var2);
  const Type* var2_type = var2->type().Get()->AsType();
  ASSERT_TRUE(var2_type);
  EXPECT_EQ("volatile my_ns::Struct", var2_type->GetFullName());

  // The lexical scope should have no other children.
  EXPECT_TRUE(inner->inner_blocks().empty());
}

// Tests both nullptr_t and typedef decoding (which is how it's encoded).
TEST(DwarfSymbolFactory, NullPtrTTypedef) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetNullPtrT function.
  const char kGetNullPtrT[] = "GetNullPtrT";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetNullPtrT);
  ASSERT_TRUE(function);

  // The return type should be nullptr_t.
  auto* nullptr_t_type = function->return_type().Get()->AsType();
  ASSERT_TRUE(nullptr_t_type);
  EXPECT_EQ("std::nullptr_t", nullptr_t_type->GetFullName());

  // The standard defined nullptr_t as "typedef decltype(nullptr) nullptr_t"
  auto* typedef_type = nullptr_t_type->AsModifiedType();
  ASSERT_TRUE(typedef_type);
  EXPECT_EQ(DwarfTag::kTypedef, typedef_type->tag());

  // Check the type underlying the typedef.
  auto* underlying = typedef_type->modified().Get()->AsType();
  ASSERT_TRUE(underlying);
  EXPECT_EQ("decltype(nullptr)", underlying->GetFullName());

  // Currently Clang defines this as an "unspecified" type. Since this isn't specified, it's
  // possible this may change in the future, but if it does we need to check to make sure everything
  // works properly.
  EXPECT_EQ(DwarfTag::kUnspecifiedType, underlying->tag());

  // The decoder should have forced the size to be the size of a pointer.
  EXPECT_EQ(8u, underlying->byte_size());
}

TEST(DwarfSymbolFactory, TemplateParams) {
  auto module_symbols =
      fxl::MakeRefCounted<ModuleSymbolsImpl>(TestSymbolModule::GetTestFileName(), "", "");
  Err err = module_symbols->Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetTemplate() function.
  const char kGetTemplate[] = "GetTemplate";
  fxl::RefPtr<const Function> function = GetFunctionWithName(module_symbols, kGetTemplate);
  ASSERT_TRUE(function);

  // The return type should be our collection
  auto* my_template = function->return_type().Get()->AsCollection();
  ASSERT_TRUE(my_template);
  EXPECT_EQ("MyTemplate<my_ns::Struct, 42>", my_template->GetFullName());

  // There should be two template parameters.
  ASSERT_EQ(2u, my_template->template_params().size());

  // The first one is "T = my_ns::Struct".
  auto first_param = my_template->template_params()[0].Get()->AsTemplateParameter();
  ASSERT_TRUE(first_param);
  EXPECT_EQ("T", first_param->GetAssignedName());
  EXPECT_EQ("T", first_param->GetFullName());

  auto first_type = first_param->type().Get()->AsType();
  ASSERT_TRUE(first_type);
  EXPECT_EQ("my_ns::Struct", first_type->GetFullName());

  // The second one is "i = int(42)".
  auto second_param = my_template->template_params()[1].Get()->AsTemplateParameter();
  ASSERT_TRUE(second_param);
  EXPECT_EQ("i", second_param->GetAssignedName());
  EXPECT_EQ("i", second_param->GetFullName());

  auto second_type = second_param->type().Get()->AsType();
  ASSERT_TRUE(second_type);
  EXPECT_EQ("int", second_type->GetFullName());
}

// TODO(brettw) test using statements. See GetUsing() in type_test.cc

}  // namespace zxdb
