// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/arch_x86.h"

#include <sstream>

#include "src/lib/fxl/strings/string_printf.h"

namespace debug_ipc {

// Debug functions -------------------------------------------------------------

std::string DR6ToString(uint64_t dr6) {
  return fxl::StringPrintf(
      "0x%lx: B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d", dr6, X86_FLAG_VALUE(dr6, DR6B0),
      X86_FLAG_VALUE(dr6, DR6B1), X86_FLAG_VALUE(dr6, DR6B2), X86_FLAG_VALUE(dr6, DR6B3),
      X86_FLAG_VALUE(dr6, DR6BD), X86_FLAG_VALUE(dr6, DR6BS), X86_FLAG_VALUE(dr6, DR6BT));
}

std::string DR7ToString(uint64_t dr7) {
  return fxl::StringPrintf(
      "0x%lx: L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, "
      "GE=%d, GD=%d, R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, LEN2=%d, "
      "R/W3=%d, LEN3=%d",
      dr7, X86_FLAG_VALUE(dr7, DR7L0), X86_FLAG_VALUE(dr7, DR7G0), X86_FLAG_VALUE(dr7, DR7L1),
      X86_FLAG_VALUE(dr7, DR7G1), X86_FLAG_VALUE(dr7, DR7L2), X86_FLAG_VALUE(dr7, DR7G2),
      X86_FLAG_VALUE(dr7, DR7L3), X86_FLAG_VALUE(dr7, DR7G3), X86_FLAG_VALUE(dr7, DR7LE),
      X86_FLAG_VALUE(dr7, DR7GE), X86_FLAG_VALUE(dr7, DR7GD), X86_FLAG_VALUE(dr7, DR7RW0),
      X86_FLAG_VALUE(dr7, DR7LEN0), X86_FLAG_VALUE(dr7, DR7RW1), X86_FLAG_VALUE(dr7, DR7LEN1),
      X86_FLAG_VALUE(dr7, DR7RW2), X86_FLAG_VALUE(dr7, DR7LEN2), X86_FLAG_VALUE(dr7, DR7RW3),
      X86_FLAG_VALUE(dr7, DR7LEN3));
}

}  // namespace debug_ipc
