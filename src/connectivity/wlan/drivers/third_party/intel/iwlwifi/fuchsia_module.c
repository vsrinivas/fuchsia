// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_module.h"

#include <lib/sync/completion.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

static const size_t kModuleNameMax = 256;

zx_status_t iwl_module_request(const char* name, ...) {
  va_list ap;
  va_start(ap, name);
  char module_name[kModuleNameMax];
  vsnprintf(module_name, sizeof(module_name), name, ap);
  va_end(ap);

  // Fuchsia does not build the other IWL driver components as separate modules, so we load them
  // directly instead.
  if (strcmp(module_name, "iwlmvm") == 0) {
    return iwl_mvm_init();
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t iwl_firmware_request(struct device* dev, const char* name, struct firmware* firmware) {
  zx_status_t status = ZX_OK;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size = 0;
  if ((status = load_firmware(dev->zxdev, name, &vmo, &size)) != ZX_OK) {
    return status;
  }

  uintptr_t vaddr = 0;
  if ((status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &vaddr)) !=
      ZX_OK) {
    IWL_ERR(drv, "Failed to map firmware VMO: %s\n", zx_status_get_string(status));
    zx_handle_close(vmo);
    return status;
  }

  firmware->vmo = vmo;
  firmware->data = (uint8_t*)vaddr;
  firmware->size = size;
  return ZX_OK;
}

zx_status_t iwl_firmware_request_nowait(struct device* dev, const char* name,
                                        void (*cont)(struct firmware* firmware, void* context),
                                        void* context) {
  // Fuchsia does support asynchronous firmware loading, but it makes for a rather complicated
  // threading model when the reply to ddk::Bind (where firmware typically loading occurs) can also
  // be threaded.  To simplify things then we'll just do this synchronously, and rely on threading
  // the entire ddk::Bind call if asynchronous loading is desired.
  zx_status_t status = ZX_OK;
  struct firmware fw = {};
  if ((status = iwl_firmware_request(dev, name, &fw)) != ZX_OK) {
    return status;
  }
  (*cont)(&fw, context);
  return ZX_OK;
}

zx_status_t iwl_firmware_release(struct firmware* firmware) {
  zx_status_t status = ZX_OK;
  if (firmware->vmo != ZX_HANDLE_INVALID) {
    if ((status = zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)firmware->data, firmware->size)) !=
        ZX_OK) {
      IWL_ERR(NULL, "Failed to unmap firmware VMO: %s\n", zx_status_get_string(status));
      return status;
    }
    zx_handle_close(firmware->vmo);

    firmware->vmo = ZX_HANDLE_INVALID;
    firmware->data = NULL;
    firmware->size = 0;
  }

  return ZX_OK;
}
