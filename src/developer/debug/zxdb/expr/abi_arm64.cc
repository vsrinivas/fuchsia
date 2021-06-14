// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/abi_arm64.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

using debug_ipc::RegisterID;

std::optional<Abi::RegisterReturn> AbiArm64::GetReturnRegisterForBaseType(
    const BaseType* base_type) {
  switch (base_type->base_type()) {
    case BaseType::kBaseTypeFloat:
      // Floats are returned as the low bits of the "v0" register according to the byte size.
      switch (base_type->byte_size()) {
        case 4:
          return RegisterReturn{.reg = RegisterID::kARMv8_s0, .base_type = RegisterReturn::kFloat};
        case 8:
          return RegisterReturn{.reg = RegisterID::kARMv8_d0, .base_type = RegisterReturn::kFloat};
      }
      return std::nullopt;

    case BaseType::kBaseTypeBoolean:
    case BaseType::kBaseTypeSigned:
    case BaseType::kBaseTypeSignedChar:
    case BaseType::kBaseTypeUnsigned:
    case BaseType::kBaseTypeUnsignedChar:
    case BaseType::kBaseTypeUTF:
      if (base_type->byte_size() <= 8) {
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

std::optional<AbiArm64::CollectionReturn> AbiArm64::GetCollectionReturnLocation(
    const Collection* collection) {
  // ARM doesn't have a return register that indicates the address of a returned structure or class.
  // This is only passed as an input register and can be clobbered by the caller. As a result, we
  // will need to store the general registers before the call to be able to decode this case. This
  // is sommethign we can do while stepping, but can't always work in general.
  return std::nullopt;
}

}  // namespace zxdb
