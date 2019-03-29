// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/function_type.h"

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

FunctionType::FunctionType(LazySymbol return_type,
                           std::vector<LazySymbol> parameters)
    : Type(DwarfTag::kSubroutineType),
      return_type_(std::move(return_type)),
      parameters_(std::move(parameters)) {
  // The byte size is the size of a pointer on the target platform.
  set_byte_size(sizeof(uint64_t));
}

FunctionType::~FunctionType() = default;

const FunctionType* FunctionType::AsFunctionType() const { return this; }

std::string FunctionType::ComputeFullNameForFunctionPtr(
    const std::string& container) const {
  std::string result = ComputeReturnTypeString();
  result.push_back(' ');

  // Name part (*) or (Class::*)
  result.push_back('(');
  if (container.empty()) {
    result.push_back('*');
  } else {
    result += container;
    result += "::*";
  }
  result.push_back(')');

  result += ComputeParameterString();
  return result;
}

std::string FunctionType::ComputeFullName() const {
  // Generally this shouldn't be called because pointers to member functions
  // and pointers to class members both have special case code paths that end
  // up in ComputeFullNameForFunctionPtr(). But in case the user dereferences a
  // function pointer, provide a reasonable name (GDB does something similar).
  return ComputeReturnTypeString() + ComputeParameterString();
}

std::string FunctionType::ComputeReturnTypeString() const {
  std::string result;
  if (return_type_) {
    if (const Type* return_type_ptr = return_type_.Get()->AsType())
      result += return_type_ptr->GetFullName();
    else
      result += "<invalid>";
  } else {
    result += "void";
  }
  return result;
}

std::string FunctionType::ComputeParameterString() const {
  std::string result("(");
  for (size_t i = 0; i < parameters_.size(); i++) {
    if (i > 0)
      result += ", ";

    const Type* param_type = nullptr;
    if (const Variable* param_var = parameters_[i].Get()->AsVariable())
      param_type = param_var->type().Get()->AsType();
    if (param_type)
      result += param_type->GetFullName();
    else
      result += "<invalid>";
  }
  result.push_back(')');
  return result;
}

}  // namespace zxdb
