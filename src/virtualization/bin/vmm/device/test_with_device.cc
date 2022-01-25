// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/test_with_device.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>

#include <algorithm>

#include "src/virtualization/bin/vmm/device/config.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

zx_status_t TestWithDevice::LaunchDevice(
    const std::string& url, size_t phys_mem_size,
    fuchsia::virtualization::hardware::StartInfo* start_info,
    std::unique_ptr<sys::testing::EnvironmentServices> env_services) {
  if (!env_services) {
    env_services = CreateServices();
  }

  // Generate an environment label from the URL, but remove path separator
  // characters which aren't allowed in the label.
  std::string env_label = "realm:" + url;
  std::replace(env_label.begin(), env_label.end(), '/', ':');

  // Create test environment.
  enclosing_environment_ = CreateNewEnclosingEnvironment(env_label, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment_.get());

  zx::channel request;
  services_ = sys::ServiceDirectory::CreateWithRequest(&request);

  // Create device process.
  fuchsia::sys::LaunchInfo launch_info{.url = url, .directory_request = std::move(request)};
  component_controller_ = enclosing_environment_->CreateComponent(std::move(launch_info));

  // Wait for component to start; because tests may use synchronous bindings, this is necessary to
  // avoid a race condition where appmgr sends a request to connect to the loader service in
  // the env hosted in this test process, which won't be processed if the test is blocked in
  // a sync call.
  component_controller_.events().OnDirectoryReady = [this]() { QuitLoop(); };
  RunLoop();

  zx_status_t status = zx::event::create(0, &event_);

  if (status != ZX_OK) {
    return status;
  }
  status = event_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, &start_info->event);
  if (status != ZX_OK) {
    return status;
  }

  // Setup guest physical memory.
  zx::vmo vmo;
  status = zx::vmo::create(phys_mem_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create VMO " << status;
    return status;
  }
  status = vmo.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &start_info->vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate VMO " << status;
    return status;
  }
  return phys_mem_.Init(std::move(vmo));
}

zx_status_t TestWithDevice::WaitOnInterrupt() {
  std::optional<zx_status_t> opt_status;
  zx_signals_t signals = VirtioQueue::InterruptAction::TRY_INTERRUPT << kDeviceInterruptShift;

  async::Wait wait(event_.get(), signals, 0 /*options*/,
                   [&](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
                     opt_status = status;
                     QuitLoop();
                   });

  zx_status_t status = wait.Begin(dispatcher());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Wait Begin failed: " << status;
    return status;
  }

  if (RunLoopWithTimeout(zx::sec(10))) {
    FX_LOGS(ERROR) << "Run loop timed out";
    return ZX_ERR_TIMED_OUT;
  }

  if (!opt_status) {
    FX_LOGS(ERROR) << "Optional status not available";
    return ZX_ERR_INTERNAL;
  }

  if (opt_status != ZX_OK) {
    FX_LOGS(ERROR) << "Optional status: " << status;
    return opt_status.value();
  }

  event_.signal(signals, 0);

  return ZX_OK;
}

// TODO(fxbug.dev/72386): Remove TestWithDevice and rename TestWithDeviceV2 to TestWithDevice once
// all device_tests are migrated to v2 components
zx_status_t TestWithDeviceV2::WaitOnInterrupt() {
  std::optional<zx_status_t> opt_status;
  zx_signals_t signals = VirtioQueue::InterruptAction::TRY_INTERRUPT << kDeviceInterruptShift;

  async::Wait wait(event_.get(), signals, 0 /*options*/,
                   [&](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
                     opt_status = status;
                     QuitLoop();
                   });

  zx_status_t status = wait.Begin(dispatcher());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Wait Begin failed: " << status;
    return status;
  }

  if (RunLoopWithTimeout(zx::sec(10))) {
    FX_LOGS(ERROR) << "Run loop timed out";
    return ZX_ERR_TIMED_OUT;
  }

  if (!opt_status) {
    FX_LOGS(ERROR) << "Optional status not available";
    return ZX_ERR_INTERNAL;
  }

  if (opt_status != ZX_OK) {
    FX_LOGS(ERROR) << "Optional status: " << status;
    return opt_status.value();
  }

  event_.signal(signals, 0);

  return ZX_OK;
}

zx_status_t TestWithDeviceV2::MakeStartInfo(
    size_t phys_mem_size, fuchsia::virtualization::hardware::StartInfo* start_info) {
  // Setup device interrupt event.
  zx_status_t status = zx::event::create(0, &event_);

  if (status != ZX_OK) {
    return status;
  }
  status = event_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, &start_info->event);
  if (status != ZX_OK) {
    return status;
  }

  // Setup guest physical memory.
  zx::vmo vmo;
  status = zx::vmo::create(phys_mem_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create VMO " << status;
    return status;
  }
  status = vmo.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &start_info->vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate VMO " << status;
    return status;
  }
  return phys_mem_.Init(std::move(vmo));
}
