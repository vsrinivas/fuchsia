// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_

#include <iosfwd>
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

  using VariantType = std::variant<Err, NoInfo, JumpInfo, LiteralInfo, Callback0, Callback1,
                                   Callback2, CallbackNInfo, AsyncCallback0, AsyncCallback1,
                                   AsyncCallback2, AsyncCallbackNInfo>;

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

using VmStream = std::vector<VmOp>;

std::ostream& operator<<(std::ostream&, const VmOp& op);

// Shared implementation for VmBytecodeForwardJump and VmBytecodeForwardJumpIfFalse below.
class VmBytecodeForwardJumper {
 public:
  VmBytecodeForwardJumper(VmStream& stream, VmOpType op);
  ~VmBytecodeForwardJumper();

  void JumpToHere();

 private:
  VmStream& stream_;
  size_t jump_source_index_;
};

// These helper classes assist in filling out a forward jump where the destination of the jump
// is not yet known.
//
// When the class is instantiated, the corresponding jump instruction is emitted with an invalid
// destination. When the stream has been appended such that the destination of the jump is now the
// end of the stream, call JumpToHere() which will fill in the current stream index into the
// destination of the previously emitted instruction.
//
// This will assert if you forget to call JumpToHere() and the class goes out of scope.
//
//   stream.push_back(...);
//   VmBytecodeForwardJump jump_out(stream);
//
//   stream.push_back(...);  // More instructions to jump over.
//
//   jump_out.JumpToHere();  // The previous jump should end up here.
class VmBytecodeForwardJump : public VmBytecodeForwardJumper {
 public:
  explicit VmBytecodeForwardJump(VmStream& stream)
      : VmBytecodeForwardJumper(stream, VmOpType::kJump) {}
};
class VmBytecodeForwardJumpIfFalse : public VmBytecodeForwardJumper {
 public:
  explicit VmBytecodeForwardJumpIfFalse(VmStream& stream)
      : VmBytecodeForwardJumper(stream, VmOpType::kJumpIfFalse) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_OP_H_
