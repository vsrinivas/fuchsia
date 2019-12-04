// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_helpers.h"

#include "src/lib/fxl/logging.h"

namespace debug_agent {
namespace arch {

namespace {

// Returns 0 on an invalid alignment.
size_t GetAlignment(size_t size) {
  if (size < 1)
    return 0;

  if (size == 1) {
    return 1;
  } else if (size == 2) {
    return 2;
  } else if (size <= 4) {
    return 4;
  } else if (size <= 8) {
    return 8;
  } else {
    return 0;
  }
}

// clang-format off
// Gets the next alignment a watchpoint might use
size_t GetNextSize(uint64_t size) {
  switch (size) {
    case 1: return 2;
    case 2: return 4;
    case 4: return 8;
    case 8: return 0;
  }

  FXL_NOTREACHED();
  return 0;
}

uint64_t GetMask(uint64_t size) {
  switch (size) {
    case 1: return 0;
    case 2: return 0b1;
    case 4: return 0b11;
    case 8: return 0b111;
  }

  FXL_NOTREACHED();
  return 0;
}
// clang-format on

}  // namespace

std::optional<debug_ipc::AddressRange> AlignRange(const debug_ipc::AddressRange& range) {
  uint64_t size = range.end() - range.begin();
  uint64_t alignment = GetAlignment(size);
  if (alignment == 0)
    return std::nullopt;

  // Check if the range is already aligned.
  uint64_t mask = GetMask(alignment);
  uint64_t aligned_address = range.begin() & ~mask;
  if (aligned_address == range.begin())
    return debug_ipc::AddressRange{range.begin(), range.begin() + alignment};

  // Check if the aligned address + the current size gets the job done.
  if (aligned_address <= range.begin() && (aligned_address + alignment) >= range.end())
    return debug_ipc::AddressRange{aligned_address, aligned_address + alignment};

  // See if we can over align this with a range.

  uint64_t next_size = GetNextSize(alignment);
  while (next_size != 0) {
    // Align the address to the new size.
    aligned_address = aligned_address & ~GetMask(next_size);

    // We see if we can cover the range with this size.
    if (aligned_address > range.begin() || (aligned_address + next_size) < range.end()) {
      next_size = GetNextSize(next_size);
      continue;
    }

    // The range fits.
    break;
  }

  // |next_size| == 0 means that there is no range the extends this range.
  if (next_size == 0)
    return std::nullopt;
  return debug_ipc::AddressRange(aligned_address, aligned_address + next_size);
}

WatchpointInstallationResult CreateResult(zx_status_t status,
                                          debug_ipc::AddressRange installed_range, int slot) {
  WatchpointInstallationResult result = {};
  result.status = status;
  result.installed_range = installed_range;
  result.slot = slot;

  return result;
}

}  // namespace arch
}  // namespace debug_agent
