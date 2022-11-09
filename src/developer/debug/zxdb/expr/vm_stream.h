// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_STREAM_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_STREAM_H_

#include <iosfwd>
#include <vector>

#include "src/developer/debug/zxdb/expr/vm_op.h"

namespace zxdb {

// Represents a program (a stream of operations).
using VmStream = std::vector<VmOp>;

// Debug formatting capabilities.
std::ostream& operator<<(std::ostream&, const VmOp& op);
std::string VmStreamToString(const VmStream& stream);

// Shared implementation for VmBytecodeForwardJump and VmBytecodeForwardJumpIfFalse below.
class VmBytecodeForwardJumper {
 public:
  VmBytecodeForwardJumper() = default;
  VmBytecodeForwardJumper(VmStream* stream, VmOpType op);
  ~VmBytecodeForwardJumper();

  // This will be a no-op if the source was never set (either by the constructor or SetSource()).
  void JumpToHere();

 protected:
  void SetSourceAndOp(VmStream* stream, VmOpType op);

 private:
  VmStream* stream_ = nullptr;
  size_t jump_source_index_ = VmOp::kBadJumpDest;
};

// These helper classes assist in filling out a forward jump where the destination of the jump
// is not yet known.
//
// When used, the corresponding jump instruction is emitted with an invalid destination. When the
// stream has been appended such that the destination of the jump is now the end of the stream, call
// JumpToHere() which will fill in the current stream index into the destination of the previously
// emitted instruction.
//
// "Using" means either instantiating it with the constructor that takes parameters or using
// SetSource(). SetSource() is provided for jumps that may be conditionally included: if the
// zero-arg constructor is called and SetSource() is never called, nothing will happen when the
// destination is known.
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
  VmBytecodeForwardJump() = default;
  explicit VmBytecodeForwardJump(VmStream* stream)
      : VmBytecodeForwardJumper(stream, VmOpType::kJump) {}

  void SetSource(VmStream* stream) { SetSourceAndOp(stream, VmOpType::kJump); }
};
class VmBytecodeForwardJumpIfFalse : public VmBytecodeForwardJumper {
 public:
  VmBytecodeForwardJumpIfFalse() = default;
  explicit VmBytecodeForwardJumpIfFalse(VmStream* stream)
      : VmBytecodeForwardJumper(stream, VmOpType::kJumpIfFalse) {}

  void SetSource(VmStream* stream) { SetSourceAndOp(stream, VmOpType::kJumpIfFalse); }
};
class VmBytecodePushBreak : public VmBytecodeForwardJumper {
 public:
  VmBytecodePushBreak() = default;
  explicit VmBytecodePushBreak(VmStream* stream)
      : VmBytecodeForwardJumper(stream, VmOpType::kPushBreak) {}

  void SetSource(VmStream* stream) { SetSourceAndOp(stream, VmOpType::kPushBreak); }
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_STREAM_H_
