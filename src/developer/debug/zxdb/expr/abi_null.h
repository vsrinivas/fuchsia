// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_NULL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_NULL_H_

#include "src/developer/debug/zxdb/expr/abi.h"

namespace zxdb {

// A generic "null" ABI that returns empty or generic ABI results. Used for defaults and tests.
class AbiNull : public Abi {
 public:
  // Abi implementation.
  debug_ipc::RegisterID GetReturnRegisterForMachineInt() const final {
    return debug_ipc::RegisterID::kUnknown;
  }
  std::optional<RegisterReturn> GetReturnRegisterForBaseType(const BaseType* base_type) final {
    return std::nullopt;
  }
  std::optional<CollectionReturn> GetCollectionReturnLocation(const Collection* collection) final {
    return std::nullopt;
  }
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_NULL_H_
