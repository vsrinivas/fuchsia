// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <sys/uio.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include "standalone.h"

// libc init and io stubs
// The reason these are here is that the "core" tests intentionally do not
// use fdio. See ./README.md.

static zx_handle_t root_resource;
static zx_handle_t mmio_root_resource;
static zx_handle_t system_root_resource;

__EXPORT
void __libc_extensions_init(uint32_t count, zx_handle_t handle[], uint32_t info[]) {
  for (unsigned n = 0; n < count; n++) {
    if (info[n] == PA_HND(PA_RESOURCE, 0)) {
      root_resource = handle[n];
      handle[n] = 0;
      info[n] = 0;
    }
    if (info[n] == PA_HND(PA_MMIO_RESOURCE, 0)) {
      mmio_root_resource = handle[n];
      handle[n] = 0;
      info[n] = 0;
    }
    if (info[n] == PA_HND(PA_SYSTEM_RESOURCE, 0)) {
      system_root_resource = handle[n];
      handle[n] = 0;
      info[n] = 0;
    }
  }
  if (root_resource == ZX_HANDLE_INVALID) {
    static const char kStandaloneMsg[] =
        "*** Standalone core-tests must run directly from userboot ***\n";
    zx_debug_write(kStandaloneMsg, sizeof(kStandaloneMsg) - 1);
    __builtin_trap();
  } else {
    StandaloneInitIo(root_resource);
  }
}

__EXPORT
zx_handle_t get_root_resource(void) { return root_resource; }

__EXPORT
zx_handle_t get_mmio_root_resource(void) { return mmio_root_resource; }

__EXPORT
zx_handle_t get_system_root_resource(void) { return system_root_resource; }
