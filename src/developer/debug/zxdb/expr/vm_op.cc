// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_op.h"

#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <sstream>

namespace zxdb {

void VmOp::SetJumpDest(uint32_t dest) {
  FX_DCHECK(op == VmOpType::kJump || op == VmOpType::kJumpIfFalse);
  std::get<JumpInfo>(info).dest = dest;
}

std::ostream& operator<<(std::ostream& out, const VmOp& op) {
  out << op.op << "(";
  switch (op.op) {
    case VmOpType::kError:
      break;
    case VmOpType::kUnary:
    case VmOpType::kBinary:
      out << op.token.value();
      break;
    case VmOpType::kExpandRef:
    case VmOpType::kDrop:
    case VmOpType::kDup:
      break;
    case VmOpType::kLiteral:
      out << std::get<VmOp::LiteralInfo>(op.info).value;
      break;
    case VmOpType::kJump:
    case VmOpType::kJumpIfFalse:
      out << std::get<VmOp::JumpInfo>(op.info).dest;
      break;
    case VmOpType::kGetLocal:
    case VmOpType::kSetLocal:
    case VmOpType::kPopLocals:
      out << std::get<VmOp::LocalInfo>(op.info).slot;
      break;
    case VmOpType::kCallback0:
    case VmOpType::kCallback1:
    case VmOpType::kCallback2:
    case VmOpType::kCallbackN:
    case VmOpType::kAsyncCallback0:
    case VmOpType::kAsyncCallback1:
    case VmOpType::kAsyncCallback2:
    case VmOpType::kAsyncCallbackN:
    case VmOpType::kLast:
      break;
  }

  out << ")";
  return out;
}

std::string VmStreamToString(const VmStream& stream) {
  std::ostringstream out;
  for (size_t i = 0; i < stream.size(); i++) {
    out << i << ": " << stream[i] << "\n";
  }
  return out.str();
}

VmBytecodeForwardJumper::VmBytecodeForwardJumper(VmStream& stream, VmOpType op)
    : stream_(stream), jump_source_index_(stream_.size()) {
  stream_.push_back(VmOp{.op = op, .info = VmOp::JumpInfo{.dest = VmOp::kBadJumpDest}});
}

VmBytecodeForwardJumper::~VmBytecodeForwardJumper() {
  // If this hits, you forgot to call JumpToHere().
  FX_DCHECK(jump_source_index_ == VmOp::kBadJumpDest);
}

void VmBytecodeForwardJumper::JumpToHere() {
  // If this hits you called JumpToHere() twice on the same object.
  FX_DCHECK(jump_source_index_ != VmOp::kBadJumpDest);

  stream_[jump_source_index_].SetJumpDest(stream_.size());

  // Help catch usage errors later.
  jump_source_index_ = VmOp::kBadJumpDest;
}

}  // namespace zxdb
