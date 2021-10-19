// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gwp_asan_info.h"

#include "allocator_config.h"

// These are defined in //third_party/scudo/src/wrappers_c.cpp but not declared in any header file.
extern "C" void malloc_postinit();
extern HIDDEN scudo::Allocator<scudo::Config, malloc_postinit> Allocator;

HIDDEN gwp_asan::LibcGwpAsanInfo __libc_gwp_asan_info;

extern "C" HIDDEN void __libc_init_gwp_asan() {
  // Ensure GWP-ASan is initialized.
  Allocator.initThreadMaybe();

  __libc_gwp_asan_info.state = Allocator.getGwpAsanAllocatorState();
  __libc_gwp_asan_info.metadata = Allocator.getGwpAsanAllocationMetadata();
}
