// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_

#include <optional>

#include "src/developer/debug/ipc/register_desc.h"

namespace zxdb {

class BaseType;
class Collection;

class Abi {
 public:
  struct RegisterReturn {
    // Register the value is returned in.
    debug_ipc::RegisterID reg = debug_ipc::RegisterID::kUnknown;

    // The fundamental type of the register. The controls how the data in the register is
    // interpreted to convert to the return type.
    enum RegisterBaseType {
      kFloat,
      kInt,
    };
    RegisterBaseType base_type = kInt;
  };

  // Information about how a collection is returned on the platform. This is a structure because it
  // will need to be enhanced in the future. It currently doesn't support several cases:
  //
  //  - On x64 and ARM collections <= 16 bytes are returned in registers:
  //
  //     - On ARM the collection is effectively memcpy'd into the registers and this should be
  //       straightforward to implement in the future.
  //
  //     - On x64 the allocation is more complicated and the collection members are taken apart and
  //       individually assigned to registers according to their type. This will be more difficult
  //       to implement in general, but we should be able to implement a one-element collection
  //       about as easily as the ARM case. This will give us some useful collections like
  //       smart pointers and handle wrappers.
  //
  //  - On ARM64 non-register collections are placed into memory indicated by the caller in x8 at
  //    the time of the function call. No information about this is guaranteed to be returned so
  //    we would need to indicate a saved register value.
  struct CollectionReturn {
    // The register which, upon return, points to the place where the called function placed the
    // collection.
    debug_ipc::RegisterID addr_return_reg = debug_ipc::RegisterID::kUnknown;
  };

  virtual ~Abi() = default;

  // Returns the register used to return a machine word like a pointer or a "regular"-sized integer.
  virtual debug_ipc::RegisterID GetReturnRegisterForMachineInt() const = 0;

  // Returns the register information for returning the given base type from a function call.
  //
  // Returns nullopt if the base type is unsupported or the value doesn't fit into a single register
  // (for example, 128 bit numbers are often split across several registers).
  virtual std::optional<RegisterReturn> GetReturnRegisterForBaseType(const BaseType* base_type) = 0;

  // Returns the information about how the given collection is returned. Returns nullopt if the
  // debugger can't compute this.
  virtual std::optional<CollectionReturn> GetCollectionReturnLocation(
      const Collection* collection) = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_
