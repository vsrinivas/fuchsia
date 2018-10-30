// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/device/test_with_device.h"
#include "garnet/lib/machina/device/config.h"
#include "garnet/lib/machina/device/virtio_queue.h"

zx_status_t TestWithDevice::LaunchDevice(
    const std::string& url, size_t phys_mem_size,
    fuchsia::guest::device::StartInfo* start_info) {
  // Create test environment.
  enclosing_environment_ =
      CreateNewEnclosingEnvironment(url + "-realm", CreateServices());
  bool started = WaitForEnclosingEnvToStart(enclosing_environment_.get());
  if (!started) {
    return ZX_ERR_TIMED_OUT;
  }

  // Create device process.
  fuchsia::sys::LaunchInfo launch_info{
      .url = url,
      .directory_request = services.NewRequest(),
  };
  component_controller_ =
      enclosing_environment_->CreateComponent(std::move(launch_info));

  // Setup device interrupt event.
  zx_status_t status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    return status;
  }
  status =
      event_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, &start_info->event);
  if (status != ZX_OK) {
    return status;
  }

  // Setup guest physical memory.
  status = phys_mem_.Init(phys_mem_size);
  if (status != ZX_OK) {
    return status;
  }
  return phys_mem_.vmo().duplicate(
      ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &start_info->vmo);
}

zx_status_t TestWithDevice::WaitOnInterrupt() {
  zx::time deadline = zx::deadline_after(zx::sec(10));
  zx_signals_t signals = machina::VirtioQueue::InterruptAction::TRY_INTERRUPT
                         << machina::kDeviceInterruptShift;
  zx_signals_t pending;
  zx_status_t status = event_.wait_one(signals, deadline, &pending);
  if (status != ZX_OK) {
    return status;
  }
  if (!(pending & signals)) {
    return ZX_ERR_BAD_STATE;
  }
  return event_.signal(pending, 0);
}
