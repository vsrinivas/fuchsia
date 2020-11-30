// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
#define SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_

#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>

static constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";

static inline fuchsia::sysinfo::SysInfoSyncPtr get_sysinfo() {
  fuchsia::sysinfo::SysInfoSyncPtr device;
  fdio_service_connect(kSysInfoPath, device.NewRequest().TakeChannel().release());
  return device;
}

static inline zx_status_t get_hypervisor_resource(zx::resource* resource) {
  fuchsia::kernel::HypervisorResourceSyncPtr svc;
  fdio_service_connect((std::string("/svc/") + fuchsia::kernel::HypervisorResource::Name_).c_str(),
                       svc.NewRequest().TakeChannel().release());
  return svc->Get(resource);
}

static inline zx_status_t get_irq_resource(zx::resource* resource) {
  fuchsia::kernel::IrqResourceSyncPtr svc;
  fdio_service_connect((std::string("/svc/") + fuchsia::kernel::IrqResource::Name_).c_str(),
                       svc.NewRequest().TakeChannel().release());
  return svc->Get(resource);
}

static inline zx_status_t get_mmio_resource(zx::resource* resource) {
  fuchsia::kernel::MmioResourceSyncPtr svc;
  fdio_service_connect((std::string("/svc/") + fuchsia::kernel::MmioResource::Name_).c_str(),
                       svc.NewRequest().TakeChannel().release());
  return svc->Get(resource);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
