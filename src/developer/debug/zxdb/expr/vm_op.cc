// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_op.h"

#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <sstream>

namespace zxdb {

void VmOp::SetJumpDest(uint32_t dest) {
  FX_DCHECK(op == VmOpType::kJump || op == VmOpType::kJumpIfFalse || op == VmOpType::kPushBreak);
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
    case VmOpType::kPushBreak:
      out << std::get<VmOp::JumpInfo>(op.info).dest;
      break;
    case VmOpType::kGetLocal:
    case VmOpType::kSetLocal:
    case VmOpType::kPopLocals:
      out << std::get<VmOp::LocalInfo>(op.info).slot;
      break;
    case VmOpType::kPopBreak:
    case VmOpType::kBreak:
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

}  // namespace zxdb
