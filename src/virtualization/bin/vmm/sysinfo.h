// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
#define SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_

#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>

static constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
static constexpr char kRootResourceSvc[] = "/svc/fuchsia.boot.RootResource";

static inline fuchsia::sysinfo::SysInfoSyncPtr get_sysinfo() {
  fuchsia::sysinfo::SysInfoSyncPtr device;
  fdio_service_connect(kSysInfoPath, device.NewRequest().TakeChannel().release());
  return device;
}

static inline zx_status_t get_root_resource(zx::resource* resource) {
  fuchsia::boot::RootResourceSyncPtr svc;
  fdio_service_connect(kRootResourceSvc, svc.NewRequest().TakeChannel().release());
  return svc->Get(resource);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
