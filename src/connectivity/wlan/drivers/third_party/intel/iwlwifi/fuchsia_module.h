// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_MODULE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_MODULE_H_

// This file contains system calls related to module and firmware loading,

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct device;

struct firmware {
  zx_handle_t vmo;
  uint8_t* data;
  size_t size;
};

// Request loading of another driver module.
zx_status_t iwl_module_request(const char* name, ...);

// Request loading of a firmware binary, synchronously.
zx_status_t iwl_firmware_request(struct device* dev, const char* name, struct firmware* firmware);

// Request loading of a firmware binary, asynchronously.
zx_status_t iwl_firmware_request_nowait(struct device* dev, const char* name,
                                        void (*cont)(struct firmware* firmware, void* context),
                                        void* context);

// Free a loaded firmware binary.
zx_status_t iwl_firmware_release(struct firmware* firmware);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_MODULE_H_
