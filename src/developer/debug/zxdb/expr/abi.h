// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_

#include <optional>

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"

namespace zxdb {

class BaseType;
class Collection;

class Abi {
 public:
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
    debug::RegisterID addr_return_reg = debug::RegisterID::kUnknown;
  };

  // Represents a component of a register that contributes to a by-value returned item. The register
  // bytes are copied from the low end.
  struct RegisterComponent {
    debug::RegisterID reg = debug::RegisterID::kUnknown;
    uint32_t bytes = 0;  // The number of bytes of the register that is actually used.
  };

  // Represents a collection returned in registers.
  struct CollectionByValueReturn {
    std::vector<RegisterComponent> regs;
  };

  virtual ~Abi() = default;

  // Returns true if the register is one of the callee-saved registers that is supposed to be
  // preserved across function calls. These registers should generally be valid in non-topmost
  // stack frames as the unwind information should be able to reconstitute them.
  virtual bool IsRegisterCalleeSaved(debug::RegisterID reg) const = 0;

  // Returns the register used to return a machine word like a pointer or a "regular"-sized integer.
  virtual debug::RegisterID GetReturnRegisterForMachineInt() const = 0;

  // Returns the register information for returning the given base type from a function call.
  //
  // Returns nullopt if the base type is unsupported or the value doesn't fit into a single register
  // (for example, 128 bit numbers are often split across several registers).
  //
  // The returned register might be larger than the base_type. In this case, the low bytes of the
  // register up to the size of the base type are used.
  virtual std::optional<debug::RegisterID> GetReturnRegisterForBaseType(
      const BaseType* base_type) = 0;

  // Returns the information about how the given collection is returned. The collection must be
  // concrete and it must be returned by reference. Returns nullopt if the debugger can't compute
  // this.
  virtual std::optional<CollectionReturn> GetCollectionReturnByRefLocation(
      const Collection* collection) = 0;

  // The collection must be concrete. Returns nullopt if the debugger can't compute this.
  virtual std::optional<CollectionByValueReturn> GetCollectionReturnByValueLocation(
      const fxl::RefPtr<EvalContext>& eval_context, const Collection* collection) = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ABI_H_
