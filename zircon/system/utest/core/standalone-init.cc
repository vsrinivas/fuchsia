// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/resource.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <string_view>

#include "standalone.h"

namespace {

zx::resource root_resource, mmio_root_resource, system_root_resource;

constexpr std::string_view kStandaloneMsg =
    "*** Standalone core-tests must run directly from userboot ***\n";

}  // namespace

extern "C" {

__EXPORT void __libc_extensions_init(uint32_t count, zx_handle_t handle[], uint32_t info[]) {
  for (unsigned n = 0; n < count; n++) {
    switch (PA_HND_TYPE(info[n])) {
      case PA_RESOURCE:
        root_resource.reset(handle[n]);
        handle[n] = ZX_HANDLE_INVALID;
        info[n] = 0;
        break;

      case PA_MMIO_RESOURCE:
        mmio_root_resource.reset(handle[n]);
        handle[n] = ZX_HANDLE_INVALID;
        info[n] = 0;
        break;

      case PA_SYSTEM_RESOURCE:
        system_root_resource.reset(handle[n]);
        handle[n] = ZX_HANDLE_INVALID;
        info[n] = 0;
        break;
    }
  }

  if (!root_resource.is_valid()) {
    zx_debug_write(kStandaloneMsg.data(), kStandaloneMsg.size());
    __builtin_trap();
  } else {
    StandaloneInitIo(zx::unowned_resource{root_resource});
  }
}

__EXPORT zx_handle_t get_root_resource(void) { return root_resource.get(); }

__EXPORT zx_handle_t get_mmio_root_resource(void) { return mmio_root_resource.get(); }

__EXPORT zx_handle_t get_system_root_resource(void) { return system_root_resource.get(); }

}  // extern "C"
