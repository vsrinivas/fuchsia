// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_stream.h"

namespace zxdb {

std::string VmStreamToString(const VmStream& stream) {
  std::ostringstream out;
  for (size_t i = 0; i < stream.size(); i++) {
    out << i << ": " << stream[i] << "\n";
  }
  return out.str();
}

VmBytecodeForwardJumper::VmBytecodeForwardJumper(VmStream* stream, VmOpType op) {
  SetSourceAndOp(stream, op);
}

VmBytecodeForwardJumper::~VmBytecodeForwardJumper() {
  // If this hits, you forgot to call JumpToHere().
  FX_DCHECK(jump_source_index_ == VmOp::kBadJumpDest);
}

void VmBytecodeForwardJumper::JumpToHere() {
  if (!stream_)
    return;  // Stream never set, this jump was never initialized.

  // If this hits you called JumpToHere() twice on the same object.
  FX_DCHECK(jump_source_index_ != VmOp::kBadJumpDest);

  (*stream_)[jump_source_index_].SetJumpDest(stream_->size());

  // Help catch usage errors later.
  jump_source_index_ = VmOp::kBadJumpDest;
}

void VmBytecodeForwardJumper::SetSourceAndOp(VmStream* stream, VmOpType op) {
  FX_DCHECK(!stream_);  // Will hit if called on an already-initialized jumper.
  stream_ = stream;
  jump_source_index_ = stream_->size();

  stream_->push_back(VmOp{.op = op, .info = VmOp::JumpInfo{.dest = VmOp::kBadJumpDest}});
}

}  // namespace zxdb
