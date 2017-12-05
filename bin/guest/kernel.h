// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_KERNEL_H_
#define GARNET_BIN_GUEST_KERNEL_H_

#include <stdint.h>
#include <stdio.h>

// NOTE(abdulla): Do not change this without testing both Linux and Zircon
// running on both arm64 and x86.
static const uintptr_t kKernelOffset = 0x100000;
static const uintptr_t kRamdiskOffset = 0x4000000;

static inline bool is_within(uintptr_t x, uintptr_t addr, uintptr_t size) {
  return x >= addr && x < addr + size;
}

static inline bool valid_location(size_t size,
                                  uintptr_t guest_ip,
                                  uintptr_t kernel_off,
                                  size_t kernel_len) {
  if (!is_within(guest_ip, kernel_off, kernel_len)) {
    fprintf(stderr, "Kernel entry point is outside of kernel location\n");
    return false;
  }
  if (kernel_off + kernel_len >= size) {
    fprintf(stderr, "Kernel location is outside of guest physical memory\n");
    return false;
  }
  if (is_within(kRamdiskOffset, kernel_off, kernel_len)) {
    fprintf(stderr, "Kernel location overlaps ramdisk location\n");
    return false;
  }
  return true;
}

#endif  // GARNET_BIN_GUEST_KERNEL_H_
