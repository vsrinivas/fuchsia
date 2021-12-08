// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/global-lock.h"

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>

namespace acpi {
void GlobalLockHandle::Create(
    acpi::Acpi* acpi, async_dispatcher_t* dispatcher,
    fidl::WireServer<fuchsia_hardware_acpi::Device>::AcquireGlobalLockCompleter::Async completer,
    uint16_t timeout) {
  // Do all of our work on a thread, to avoid potentially blocking the main async loop.
  std::thread thread([acpi, dispatcher, completer = std::move(completer), timeout]() mutable {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::GlobalLock>();
    if (endpoints.is_error()) {
      completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kError);
      return;
    }
    auto status = acpi->AcquireGlobalLock(timeout);
    if (status.is_error()) {
      completer.ReplyError(fuchsia_hardware_acpi::wire::Status(status.error_value()));
      return;
    }
    auto lock = std::make_unique<GlobalLockHandle>(acpi, status.value());

    fidl::BindServer(dispatcher, std::move(endpoints->server), std::move(lock));
    completer.ReplySuccess(std::move(endpoints->client));
  });
  thread.detach();
}

}  // namespace acpi
