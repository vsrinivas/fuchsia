// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/abi_x64.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

using debug_ipc::RegisterID;

std::optional<Abi::RegisterReturn> AbiX64::GetReturnRegisterForBaseType(const BaseType* base_type) {
  switch (base_type->base_type()) {
    case BaseType::kBaseTypeFloat:
      if (base_type->byte_size() <= 8) {
        // All normal floats use xmm0.
        return RegisterReturn{.reg = RegisterID::kX64_xmm0, .base_type = RegisterReturn::kFloat};
      }
      return std::nullopt;

    case BaseType::kBaseTypeBoolean:
    case BaseType::kBaseTypeSigned:
    case BaseType::kBaseTypeSignedChar:
    case BaseType::kBaseTypeUnsigned:
    case BaseType::kBaseTypeUnsignedChar:
    case BaseType::kBaseTypeUTF:
      if (base_type->byte_size() <= 8) {
        // All normal ints use rax.
        return RegisterReturn{.reg = GetReturnRegisterForMachineInt(),
                              .base_type = RegisterReturn::kInt};
      }
      // Larger numbers are spread across multiple registers which we don't support yet.
      return std::nullopt;

    case BaseType::kBaseTypeNone:
    case BaseType::kBaseTypeAddress:  // Not used in C.
    default:
      return std::nullopt;
  }
}

std::optional<AbiX64::CollectionReturn> AbiX64::GetCollectionReturnLocation(
    const Collection* collection) {
  // Pass-by-value collections are returned in registers which we don't support yet. Also assume
  // that anything not marked explicitly as "pass by reference" is suspicious and don't assume
  // it's passed by reference.
  if (collection->calling_convention() != Collection::kPassByReference)
    return std::nullopt;

  // Pass-by-reference collections are placed into a location indicated by the caller and that
  // location is echoed back upon return in the rax register.
  return CollectionReturn{.addr_return_reg = RegisterID::kX64_rax};
}

}  // namespace zxdb
