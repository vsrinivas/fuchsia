// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_TYPE_H_

#include <iosfwd>

namespace zxdb {

// The bytecode operation types used by VmOp.
//
// Our bytecode operations are very simple. Most of the critical logic is implemented via the
// various callback operations. The bytecode operations exist only to dop the toplevel control-flow.
//
// See VmOp for more.
enum class VmOpType {
  // Not present, unknown, or uninitialized. If the variant is set to an Err, this will be
  // the error thrown, otherwise a generic error will be set.
  kError,

  // Operators.
  kUnary,   // Unary operation that pops one element off the stack.
  kBinary,  // Binary operation that pops two elements off the stack.

  // Target data lookup.
  kExpandRef,  // Converts "Foo&" at the top of the stack to "Foo". Other types are unchanged.

  // Stack control.
  kDrop,     // Drops the top stack element.
  kDup,      // Copies and pushes the top stack element.
  kLiteral,  // Pushes the literal stored in LiteralInfo on the stack.

  // Control flow.
  kJump,
  kJumpIfFalse,  // Pops the top stack element and jumps if false.

  // Custom callbacks for other functions.
  kCallback0,       // Calls the function to get a result.
  kCallback1,       // Pops one value and passes it to the function.
  kCallback2,       // Pops two values and passes it to the function.
  kCallbackN,       // Pops N values and passes it to the function.
  kAsyncCallback0,  // Calls the function to get the result asynchronously.
  kAsyncCallback1,  // Pops one value and passes it to the function with a callback.
  kAsyncCallback2,  // Pops two values and passes it to the function with a callback.
  kAsyncCallbackN,  // Pops N values and passes it to the function with a callback.

  kLast  // Marker, not an operation.
};

// Converts the operation to a string/stream.
const char* VmOpTypeToString(VmOpType op);
std::ostream& operator<<(std::ostream&, VmOpType op);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_TYPE_H_
