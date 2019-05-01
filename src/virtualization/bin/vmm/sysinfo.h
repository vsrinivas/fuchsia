// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
#define SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_

#include <fuchsia/sysinfo/cpp/fidl.h>

static constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";

static inline fuchsia::sysinfo::DeviceSyncPtr get_sysinfo() {
  fuchsia::sysinfo::DeviceSyncPtr device;
  fdio_service_connect(kSysInfoPath,
                       device.NewRequest().TakeChannel().release());
  return device;
}

static inline zx_status_t get_root_resource(
    const fuchsia::sysinfo::DeviceSyncPtr& sysinfo, zx::resource* resource) {
  zx_status_t fidl_status;
  zx_status_t status = sysinfo->GetRootResource(&fidl_status, resource);
  if (status != ZX_OK) {
    return status;
  }
  return fidl_status;
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
