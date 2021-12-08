// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/debug.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"

namespace acpi {

class GlobalLockHandle : public fidl::WireServer<fuchsia_hardware_acpi::GlobalLock> {
 public:
  explicit GlobalLockHandle(acpi::Acpi* acpi, uint32_t handle) : acpi_(acpi), handle_(handle) {}
  ~GlobalLockHandle() override {
    auto error = acpi_->ReleaseGlobalLock(handle_);
    if (error.is_error()) {
      zxlogf(ERROR, "Failed to release global lock: %d", error.status_value());
    }
  }

  // Acquire the global lock and create a GlobalLockHandle which serves requests on the given
  // |server|. The GlobalLockHandle owns itself, and will be destroyed when the channel is closed.
  // This function does not block the calling thread. It will spawn a thread which waits for the
  // global lock, and then replies to the transaction.
  static void Create(
      acpi::Acpi* acpi, async_dispatcher_t* dispatcher,
      fidl::WireServer<fuchsia_hardware_acpi::Device>::AcquireGlobalLockCompleter::Async completer,
      uint16_t timeout = 0xffff);

 private:
  acpi::Acpi* acpi_;
  uint32_t handle_;
};

}  // namespace acpi
