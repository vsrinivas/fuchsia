// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

struct Frame {
  enum class Trust {
    kScan,     // From scanning the stack with heuristics, least reliable.
    kFP,       // From the frame pointer.
    kSSC,      // From the shadow call stack.
    kCFI,      // From call frame info / .eh_frame section.
    kContext,  // From the input / context, most reliable.
  };

  // Register status at each return site. Only known values will be included.
  Registers regs;

  // Trust level of the frame.
  Trust trust;

  // Error when unwinding this frame.
  Error error;

  // Disallow default constructors.
  Frame(Registers regs, Trust trust, Error error)
      : regs(std::move(regs)), trust(trust), error(std::move(error)) {}
};

// Unwind with given memory, modules and registers.
std::vector<Frame> Unwind(Memory* memory, const std::vector<uint64_t>& modules, Registers registers,
                          int max_depth = 50);

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_H_
