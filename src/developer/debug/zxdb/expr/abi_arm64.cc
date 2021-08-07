// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/abi_arm64.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

using debug::RegisterID;

bool AbiArm64::IsRegisterCalleeSaved(debug::RegisterID reg) const {
  switch (reg) {
    case debug::RegisterID::kARMv8_x19:
    case debug::RegisterID::kARMv8_w19:  // Include the word versions as well.
    case debug::RegisterID::kARMv8_x20:
    case debug::RegisterID::kARMv8_w20:
    case debug::RegisterID::kARMv8_x21:
    case debug::RegisterID::kARMv8_w21:
    case debug::RegisterID::kARMv8_x22:
    case debug::RegisterID::kARMv8_w22:
    case debug::RegisterID::kARMv8_x23:
    case debug::RegisterID::kARMv8_w23:
    case debug::RegisterID::kARMv8_x24:
    case debug::RegisterID::kARMv8_w24:
    case debug::RegisterID::kARMv8_x25:
    case debug::RegisterID::kARMv8_w25:
    case debug::RegisterID::kARMv8_x26:
    case debug::RegisterID::kARMv8_w26:
    case debug::RegisterID::kARMv8_x27:
    case debug::RegisterID::kARMv8_w27:
    case debug::RegisterID::kARMv8_x28:
    case debug::RegisterID::kARMv8_w28:
    case debug::RegisterID::kARMv8_x29:
    case debug::RegisterID::kARMv8_lr:
    case debug::RegisterID::kARMv8_w30:
    case debug::RegisterID::kARMv8_x30:  // Alias for LR.
    case debug::RegisterID::kARMv8_sp:
    case debug::RegisterID::kARMv8_pc:
      return true;
    default:
      return false;
  }
}

std::optional<debug::RegisterID> AbiArm64::GetReturnRegisterForBaseType(const BaseType* base_type) {
  switch (base_type->base_type()) {
    case BaseType::kBaseTypeFloat:
      // Floats are returned as the low bits of the "v0" register. The caller can extract the
      // correct number of bytes.
      if (base_type->byte_size() <= 8)
        return RegisterID::kARMv8_v0;
      return std::nullopt;

    case BaseType::kBaseTypeBoolean:
    case BaseType::kBaseTypeSigned:
    case BaseType::kBaseTypeSignedChar:
    case BaseType::kBaseTypeUnsigned:
    case BaseType::kBaseTypeUnsignedChar:
    case BaseType::kBaseTypeUTF:
      if (base_type->byte_size() <= 8)
        return GetReturnRegisterForMachineInt();

      // Larger numbers are spread across multiple registers which we don't support yet.
      return std::nullopt;

    case BaseType::kBaseTypeNone:
    case BaseType::kBaseTypeAddress:  // Not used in C.
    default:
      return std::nullopt;
  }
}

std::optional<AbiArm64::CollectionReturn> AbiArm64::GetCollectionReturnByRefLocation(
    const Collection* collection) {
  // ARM doesn't have a return register that indicates the address of a returned structure or class.
  // This is only passed as an input register and can be clobbered by the caller. As a result, we
  // will need to store the general registers before the call to be able to decode this case. This
  // is sommethign we can do while stepping, but can't always work in general.
  return std::nullopt;
}

std::optional<Abi::CollectionByValueReturn> AbiArm64::GetCollectionReturnByValueLocation(
    const fxl::RefPtr<EvalContext>& eval_context, const Collection* collection) {
  // Anything that doesn't fit into two registers is returned on the stack.
  constexpr int kMaxReturnRegs = 2;

  if (collection->byte_size() == 0 || collection->byte_size() > kMaxReturnRegs * sizeof(int64_t))
    return std::nullopt;  // Too big.

  // Collections are packed into these registers, in-order.
  const RegisterID kReturnRegs[kMaxReturnRegs] = {RegisterID::kARMv8_x0, RegisterID::kARMv8_x1};

  uint32_t remaining_bytes = collection->byte_size();

  CollectionByValueReturn result;
  for (int register_num = 0; register_num < kMaxReturnRegs && remaining_bytes > 0; register_num++) {
    uint32_t cur_bytes = std::min<uint32_t>(remaining_bytes, sizeof(uint64_t));
    result.regs.push_back({.reg = kReturnRegs[register_num], .bytes = cur_bytes});
    remaining_bytes -= cur_bytes;
  }

  return result;
}

}  // namespace zxdb
