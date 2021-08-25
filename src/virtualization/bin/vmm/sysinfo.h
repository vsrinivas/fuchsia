// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
#define SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_

#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/fdio/directory.h>

static inline zx_status_t get_interrupt_controller_info(
    fuchsia::sysinfo::InterruptControllerInfoPtr* info) {
  fuchsia::sysinfo::SysInfoSyncPtr svc;
  zx_status_t status = fdio_service_connect_by_name(fuchsia::sysinfo::SysInfo::Name_,
                                                    svc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  zx_status_t fidl_status;
  status = svc->GetInterruptControllerInfo(&fidl_status, info);
  if (status != ZX_OK) {
    return status;
  }
  return fidl_status;
}

static inline zx_status_t get_hypervisor_resource(zx::resource* resource) {
  fuchsia::kernel::HypervisorResourceSyncPtr svc;
  zx_status_t status = fdio_service_connect_by_name(fuchsia::kernel::HypervisorResource::Name_,
                                                    svc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return svc->Get(resource);
}

static inline zx_status_t get_irq_resource(zx::resource* resource) {
  fuchsia::kernel::IrqResourceSyncPtr svc;
  zx_status_t status = fdio_service_connect_by_name(fuchsia::kernel::IrqResource::Name_,
                                                    svc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return svc->Get(resource);
}

static inline zx_status_t get_mmio_resource(zx::resource* resource) {
  fuchsia::kernel::MmioResourceSyncPtr svc;
  zx_status_t status = fdio_service_connect_by_name(fuchsia::kernel::MmioResource::Name_,
                                                    svc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return svc->Get(resource);
}

static inline zx_status_t get_vmex_resource(zx::resource* resource) {
  fuchsia::kernel::VmexResourceSyncPtr svc;
  zx_status_t status = fdio_service_connect_by_name(fuchsia::kernel::VmexResource::Name_,
                                                    svc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return svc->Get(resource);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_SYSINFO_H_
