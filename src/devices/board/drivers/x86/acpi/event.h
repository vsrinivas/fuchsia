// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_EVENT_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_EVENT_H_

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fpromise/bridge.h>

#include <future>

#include "lib/ddk/debug.h"

namespace acpi {
class Device;

class NotifyEventHandler
    : public fidl::WireAsyncEventHandler<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit NotifyEventHandler(acpi::Device* device, fpromise::completer<void> teardown)
      : device_(device), teardown_(std::move(teardown)) {}
  ~NotifyEventHandler() override { teardown_.complete_ok(); }

  void on_fidl_error(fidl::UnbindInfo error) override;

 private:
  Device* device_;
  fpromise::completer<void> teardown_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_EVENT_H_
