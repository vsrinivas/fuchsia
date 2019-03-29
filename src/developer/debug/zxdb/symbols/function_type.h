// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// This class represents types for functions. This corresponds to a dwarf
// "subroutine type" entry which has no direct analog in C/C++.
//
// When referenced by a "pointer" ModifiedType class, the combination becomes a
// pointer to a function. When referenced by a MemberPtr class, the combination
// becomes a pointer to a member function.
class FunctionType final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const FunctionType* AsFunctionType() const override;

  // The return value type. This should be some kind of Type object. Will be
  // empty for void return types.
  const LazySymbol& return_type() const { return return_type_; }

  // Parameters passed to the function. These should be Variable objects.
  const std::vector<LazySymbol>& parameters() const { return parameters_; }

  // Computes the name of this function when it's a member function pointer of
  // the given type. For example, if container is "Foo", this might return
  //   "void (Foo::*)(int)"
  //
  // If |container| is empty, this will compute the name assuming it's not a
  // member pointer.
  std::string ComputeFullNameForFunctionPtr(const std::string& container) const;

 protected:
  // Symbol protected overrides.
  std::string ComputeFullName() const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(FunctionType);
  FRIEND_MAKE_REF_COUNTED(FunctionType);

  // The return type can also be a null symbol when the return type is void.
  FunctionType(LazySymbol return_type, std::vector<LazySymbol> parameters);
  virtual ~FunctionType();

  // These two functions return the strings associated with the return type
  // and parameters of this function. They are used as building blocks for the
  // other formatters.
  std::string ComputeReturnTypeString() const;
  std::string ComputeParameterString() const;

  LazySymbol return_type_;
  std::vector<LazySymbol> parameters_;
};

}  // namespace zxdb
