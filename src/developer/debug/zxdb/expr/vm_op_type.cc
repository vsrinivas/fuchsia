// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_op_type.h"

#include <iostream>

namespace zxdb {

const char* VmOpTypeToString(VmOpType op) {
  switch (op) {
    // clang-format off
    case VmOpType::kError:          return "Error";
    case VmOpType::kUnary:          return "Unary";
    case VmOpType::kBinary:         return "Binary";
    case VmOpType::kExpandRef:      return "ExpandRef";
    case VmOpType::kDrop:           return "Drop";
    case VmOpType::kDup:            return "Dup";
    case VmOpType::kLiteral:        return "Literal";
    case VmOpType::kJump:           return "Jump";
    case VmOpType::kJumpIfFalse:    return "JumpIfFalse";
    case VmOpType::kGetLocal:       return "GetLocal";
    case VmOpType::kSetLocal:       return "SetLocal";
    case VmOpType::kPopLocals:      return "PopLocals";
    case VmOpType::kCallback0:      return "Callback0";
    case VmOpType::kCallback1:      return "Callback1";
    case VmOpType::kCallback2:      return "Callback2";
    case VmOpType::kCallbackN:      return "CallbackN";
    case VmOpType::kAsyncCallback0: return "AsyncCallback0";
    case VmOpType::kAsyncCallback1: return "AsyncCallback1";
    case VmOpType::kAsyncCallback2: return "AsyncCallback2";
    case VmOpType::kAsyncCallbackN: return "AsyncCallbackN";

    case VmOpType::kLast:        return "Last <ERROR!>";  // Should not encounter.
      // clang-format on
  }
  return "";
}

std::ostream& operator<<(std::ostream& out, VmOpType op) { return out << VmOpTypeToString(op); }

}  // namespace zxdb
