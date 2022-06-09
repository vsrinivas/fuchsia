// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_MANAGER_HOST_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_MANAGER_HOST_H_

#include <assert.h>
#include <zircon/compiler.h>

#include "src/devices/board/lib/acpi/manager.h"
#include "src/devices/board/lib/acpi/test/null-iommu-manager.h"
namespace acpi {

class HostManager : public Manager {
 public:
  HostManager(acpi::Acpi* acpi, zx_device_t* acpi_root)
      : Manager(acpi, &iommu_manager_, acpi_root) {}
  ~HostManager() override {}

  async_dispatcher_t* fidl_dispatcher() override { return nullptr; }
  async::Executor& executor() override { abort(); }

 private:
  NullIommuManager iommu_manager_;
};

}  // namespace acpi

#endif
