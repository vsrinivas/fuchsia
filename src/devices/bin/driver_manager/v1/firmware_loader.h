// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_FIRMWARE_LOADER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_FIRMWARE_LOADER_H_

#include "lib/async/dispatcher.h"
#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/driver_host.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

struct LoadFirmwareResult {
  zx::vmo vmo;
  size_t size;
};

class FirmwareLoader {
 public:
  FirmwareLoader(Coordinator* coordinator, async_dispatcher_t* firmware_dispatcher,
                 std::string path_prefix);

  void LoadFirmware(const fbl::RefPtr<Device>& dev, const char* driver_libname, const char* path,
                    fit::callback<void(zx::status<LoadFirmwareResult>)> cb);

 private:
  Coordinator* coordinator_;
  async_dispatcher_t* const firmware_dispatcher_;
  std::string path_prefix_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_FIRMWARE_LOADER_H_
