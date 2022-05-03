// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_MANAGER_FUCHSIA_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_MANAGER_FUCHSIA_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>

#include "src/devices/board/lib/acpi/manager.h"

namespace acpi {

// Specialisation of ACPI manager for Fuchsia.
class FuchsiaManager : public Manager {
 public:
  FuchsiaManager(acpi::Acpi* acpi, zx_device_t* acpi_root)
      : Manager(acpi, acpi_root),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(loop_.dispatcher()) {}

  ~FuchsiaManager() override { loop_.Shutdown(); }

  zx_status_t StartFidlLoop() override { return loop_.StartThread("acpi-fidl-thread"); }

  async_dispatcher_t* fidl_dispatcher() override { return loop_.dispatcher(); }
  async::Executor& executor() override { return executor_; }

 private:
  async::Loop loop_;
  async::Executor executor_;
};

}  // namespace acpi

#endif
