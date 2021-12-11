// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/lazy_init/lazy_init.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <map>
#include <string>
#include <string_view>

#include "standalone.h"

namespace {

zx::resource root_resource, mmio_root_resource, system_root_resource;

constexpr std::string_view kStandaloneMsg =
    "*** Standalone core-tests must run directly from userboot ***\n";

lazy_init::LazyInit<std::map<std::string, zx::vmo>> gVmos;

}  // namespace

zx::unowned_vmo StandaloneGetVmo(const std::string& name) {
  auto it = gVmos->find(name);
  if (it == gVmos->end()) {
    return {};
  }
  return zx::unowned_vmo{it->second};
}

extern "C" {

__EXPORT void __libc_extensions_init(uint32_t count, zx_handle_t handle[], uint32_t info[]) {
  gVmos.Initialize();

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

      case PA_VMO_BOOTDATA:
      case PA_VMO_KERNEL_FILE: {
        // Store it in the map by VMO name for StandaloneGetVmo to find later.
        char vmo_name[ZX_MAX_NAME_LEN];
        zx_status_t status =
            zx_object_get_property(handle[n], ZX_PROP_NAME, vmo_name, sizeof(vmo_name));
        if (status == ZX_OK) {
          zx::vmo vmo{handle[n]};
          handle[n] = ZX_HANDLE_INVALID;
          info[n] = 0;

          std::string_view prop(vmo_name, sizeof(vmo_name));
          std::string name(prop.substr(0, prop.find_first_of('\0')));
          gVmos->try_emplace(std::move(name), std::move(vmo));
        }
        break;
      }
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
