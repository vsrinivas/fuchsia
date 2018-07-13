// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdio>

#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

namespace inferior_control {

// Signal values to pass over the remote serial protocol.
// See include/gdb/signals.def in the gdb tree.
// We just define the ones we use or might need.
// TODO(dje): The translation isn't always perfect, and we could define new
// ones.

enum class GdbSignal {
  kUnsupported = -1,
  kNone = 0,
  kInt = 2,
  kQuit = 3,
  kIll = 4,
  kTrap = 5,
  kAbrt = 6,
  kEmt = 7,
  kFpe = 8,
  kBus = 10,
  kSegv = 11,
  kUrg = 16,
  kStop = 17,
  kCont = 19,
  kVtalrm = 26,
  kUsr1 = 30,
  kUsr2 = 31,
};

// Maps the architecture-specific exception code to a UNIX compatible signal
// value that GDB understands. Returns kUnsupported if the current
// architecture is not currently supported.
GdbSignal ComputeGdbSignal(const zx_exception_context_t& context);

// Returns true if |context| is a single-stepping exception.
bool IsSingleStepException(const zx_exception_context_t& context);

// Dump random bits about the architecuture.
// TODO(dje): Switch to iostreams maybe later.
void DumpArch(FILE* out);

}  // namespace inferior_control
