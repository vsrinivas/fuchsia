// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include "lib/ftl/logging.h"

#include "process.h"

namespace debugserver {
namespace arch {
namespace {

// The i386 Int3 instruction.
const uint8_t kInt3 = 0xCC;

}  // namespace

bool SoftwareBreakpoint::Insert() {
  if (IsInserted()) {
    FTL_LOG(WARNING) << "Breakpoint already inserted";
    return false;
  }

  // We only support inserting the single byte Int3 instruction.
  if (kind() != 1) {
    FTL_LOG(ERROR) << "Software breakpoint kind must be 1 on amd64";
    return false;
  }

  // Read the current contents at the address that we're about to overwrite, so
  // that it can be restored later.
  uint8_t orig;
  if (!owner()->process()->ReadMemory(address(), &orig, 1)) {
    FTL_LOG(ERROR) << "Failed to obtain current contents of memory";
    return false;
  }

  // Insert the Int3 instruction.
  if (!owner()->process()->WriteMemory(address(), &kInt3, 1)) {
    FTL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  original_bytes_.push_back(orig);
  return true;
}

bool SoftwareBreakpoint::Remove() {
  if (!IsInserted()) {
    FTL_LOG(WARNING) << "Breakpoint not inserted";
    return false;
  }

  FTL_DCHECK(original_bytes_.size() == 1);

  // Restore the original contents.
  if (!owner()->process()->WriteMemory(address(), original_bytes_.data(), 1)) {
    FTL_LOG(ERROR) << "Failed to restore original instructions";
    return false;
  }

  original_bytes_.clear();
  return true;
}

bool SoftwareBreakpoint::IsInserted() const {
  return !original_bytes_.empty();
}

}  // namespace arch
}  // namespace debugserver
