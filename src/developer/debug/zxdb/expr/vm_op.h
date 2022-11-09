// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_

#include <variant>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/expr/vm_op_type.h"

namespace zxdb {

// Holds a bytecode operation type (VmOpType) and any parameters associated with it.
//
// Most bytecode machines would encode any parameters like strings constants or a binary operator
// type compactly, either in the byte string, or using a small representation of a reference to
// some other stream.
//
// Our programs are so small compared to the type of system we expect to run on we do not care
// about space. Instead, this implementation prefers a safe, simple approach. As part of this,
// each VM operation is a bytecode operation combined with a relatively large variant holding any
// ancillary data required by the operation. For many operations, this is very wasteful, but means
// we can avoid doing error-prone reading of variable data from the instruction stream and the
// decode logic becomes trivial.
//
// See vm_exec.cc for execution details.
//
// Local variables
// ---------------
//
// In most "real" stack-based bycode machines, the local variables would be stored on the stack
// and referenced by index from the "stack pointer" (the position of the stack and the entrypoint
// of the current function). It is simple and efficient.
//
// But it requires very careful tracking of where variables can be declared such that the block
// that contains them can pop its local variables when the block exits: a local variable declaration
// must never be conditionally executed since then the containing block wouldn't know whether to
// clean it up. Any mistakes will corrupt the VM stack and are difficult to debug.
//
// Our parser supports multiple languages and plays a bit fast-and-loose with where declarations can
// appear. This could be fixed but since we also care much more about debugability and simplicity
// of the parser than of performance, we have dedicated storage for local variables.
//
// As local variables are parsed, we assign each one an index based on the parser depth just like
// the stack-based approach. But these refer into a separate array specific to local variables.
// This adds a copy for each variable declaration and some extra memory allocations for the separate
// array but neither of these matter to us. The local variables created inside a scope are cleared
// by the kPopLocals command which will be emitted at the end of a block. The block knows the size
// to shrink to because it knows how many local variables are in scope at the entry to the block.
// But this approach doesn't care if a local variable declaration was skipped (that slot will just
// be unused).
//
// Example:
//
//   {              // The parser remembers the # of locals in scope at the opening of the block.
//                  // This info is saved on the block parse node for the last step.
//
//     int i = 23;  // The parser adds info about "i" and saves its index as the local variable
//                  // slot. This slot is used in the op kSetLocal.
//
//     i = 19;      // The parser looks up the variable index and uses it in a kGetLocal op.
//
//   }              // At the exit of a block, the parser emits kPopLocals to set the # locals back
//                  // the size it was at the opening of the block.
//
// Variable assignment
// -------------------
//
// Our expression language doesn't have lvalues. The assignment operator (binary operator "=")
// always fully evaluates the thing on the left-hand-side. This simplifies things by providing only
// one code path for all evaluating (rather than having a code path to compute where the thing would
// be without computing its value).
//
// All ExprValues have an ExprValueSource which tracks where they came from originally. This
// information is used to update the variable when modified. For program values this is normally a
// memory address or a register. For debugger-local variables, we keep a refptr to the source of
// the value. The LocalExprValue object is a refcounted container for local variables that can
// have such references to them.
struct VmOp {
  // Constant to default-initialize a jump destination to that's wrong.
  static constexpr uint32_t kBadJumpDest = static_cast<uint32_t>(-1);

  // Callback types.
  using Callback0 = fit::function<ErrOrValue(const fxl::RefPtr<EvalContext>& eval_context)>;
  using Callback1 =
      fit::function<ErrOrValue(const fxl::RefPtr<EvalContext>& eval_context, ExprValue param)>;
  using Callback2 = fit::function<ErrOrValue(const fxl::RefPtr<EvalContext>& eval_context,
                                             ExprValue param1, ExprValue param2)>;
  using CallbackN = fit::function<ErrOrValue(const fxl::RefPtr<EvalContext>& eval_context,
                                             std::vector<ExprValue> params)>;
  using AsyncCallback0 =
      fit::function<void(const fxl::RefPtr<EvalContext>& eval_context, EvalCallback cb)>;
  using AsyncCallback1 = fit::function<void(const fxl::RefPtr<EvalContext>& eval_context,
                                            ExprValue param, EvalCallback cb)>;
  using AsyncCallback2 = fit::function<void(const fxl::RefPtr<EvalContext>& eval_context,
                                            ExprValue first, ExprValue second, EvalCallback cb)>;
  using AsyncCallbackN = fit::function<void(const fxl::RefPtr<EvalContext>& eval_context,
                                            std::vector<ExprValue> params, EvalCallback cb)>;

  // Constructor helper functions.
  static VmOp MakeError(Err err, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kError, .token = std::move(token), .info = err};
  }
  static VmOp MakeUnary(ExprToken token) {
    return VmOp{.op = VmOpType::kUnary, .token = std::move(token)};
  }
  static VmOp MakeBinary(ExprToken token) {
    return VmOp{.op = VmOpType::kBinary, .token = std::move(token)};
  }
  static VmOp MakeExpandRef(ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kExpandRef, .token = std::move(token)};
  }
  static VmOp MakeDrop(ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kDrop, .token = std::move(token)};
  }
  static VmOp MakeDup(ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kDup, .token = std::move(token)};
  }
  static VmOp MakeLiteral(ExprValue value, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kLiteral,
                .token = std::move(token),
                .info = LiteralInfo{.value = std::move(value)}};
  }
  static VmOp MakeJump(uint32_t dest = kBadJumpDest, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kJump, .token = std::move(token), .info = JumpInfo{.dest = dest}};
  }
  static VmOp MakeJumpIfFalse(uint32_t dest = kBadJumpDest, ExprToken token = ExprToken()) {
    return VmOp{
        .op = VmOpType::kJumpIfFalse, .token = std::move(token), .info = JumpInfo{.dest = dest}};
  }
  static VmOp MakeGetLocal(uint32_t slot, ExprToken token = ExprToken()) {
    return VmOp{
        .op = VmOpType::kGetLocal, .token = std::move(token), .info = LocalInfo{.slot = slot}};
  }
  static VmOp MakeSetLocal(uint32_t slot, ExprToken token = ExprToken()) {
    return VmOp{
        .op = VmOpType::kSetLocal, .token = std::move(token), .info = LocalInfo{.slot = slot}};
  }
  static VmOp MakePopLocals(uint32_t entry_count, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kPopLocals,
                .token = std::move(token),
                .info = LocalInfo{.slot = entry_count}};
  }
  static VmOp MakeCallback0(Callback0 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kCallback0, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeCallback1(Callback1 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kCallback1, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeCallback2(Callback2 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kCallback2, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeCallbackN(int num_params, CallbackN cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kCallbackN,
                .token = std::move(token),
                .info = CallbackNInfo{.num_params = num_params, .cb = std::move(cb)}};
  }
  static VmOp MakeAsyncCallback0(AsyncCallback0 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kAsyncCallback0, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeAsyncCallback1(AsyncCallback1 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kAsyncCallback1, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeAsyncCallback2(AsyncCallback2 cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kAsyncCallback2, .token = std::move(token), .info = std::move(cb)};
  }
  static VmOp MakeAsyncCallbackN(int num_params, AsyncCallbackN cb, ExprToken token = ExprToken()) {
    return VmOp{.op = VmOpType::kAsyncCallbackN,
                .token = std::move(token),
                .info = AsyncCallbackNInfo{.num_params = num_params, .cb = std::move(cb)}};
  }

  // For operators with no extra info.
  struct NoInfo {};

  // For kLiteral.
  struct LiteralInfo {
    ExprValue value;  // Literal value to push on the stack.
  };

  // For kJump and kJumpIfFalse.
  struct JumpInfo {
    uint32_t dest = kBadJumpDest;  // Index of the destination of a jump operator.
  };

  // For kGetLocal, kSetLocal, and kPopLocals.
  struct LocalInfo {
    // Indicates the index into the local variable array of the local variable to be get/set for
    // kGetLocal and kSetLocal.
    //
    // Indicates the final size of the variable array for kPopLocals.
    //
    // See "Local Variables" at the top of this file for an overview.
    uint32_t slot = static_cast<uint32_t>(-1);
  };

  // The other callback types use the callback "using" statement above in the variant directly, but
  // the "N" versions need a place to store the number of arguments alongside it.
  struct CallbackNInfo {
    int num_params;
    CallbackN cb;
  };
  struct AsyncCallbackNInfo {
    int num_params;
    AsyncCallbackN cb;
  };

  using VariantType = std::variant<Err, NoInfo, JumpInfo, LiteralInfo, LocalInfo, Callback0,
                                   Callback1, Callback2, CallbackNInfo, AsyncCallback0,
                                   AsyncCallback1, AsyncCallback2, AsyncCallbackNInfo>;

  // Sets the destination of the this operator's jump destination to the given value. This will
  // assert if this operation is not a jump.
  //
  // This is used commonly because the destination of a jump is often unknown until additional code
  // is filled in.
  void SetJumpDest(uint32_t dest);

  // Operation.
  VmOpType op = VmOpType::kError;

  // The token that generated this operation. For binary and unary operations, this is the operator
  // itself. For things like loops, it will be the token indicating the loop ("while" for example).
  // Errors will be blamed on this token.
  ExprToken token;

  VariantType info;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_
