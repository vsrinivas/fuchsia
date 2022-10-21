// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_op.h"

#include <lib/syslog/cpp/macros.h>

#include <iostream>

namespace zxdb {

void VmOp::SetJumpDest(uint32_t dest) {
  FX_DCHECK(op == VmOpType::kJump || op == VmOpType::kJumpIfFalse);
  std::get<JumpInfo>(info).dest = dest;
}

std::ostream& operator<<(std::ostream& out, const VmOp& op) {
  out << op.op << " (" << op.token.value() << ")";
  // It might be nice to output the variant information here.
  return out;
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
