// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>

#include "src/virtualization/bin/vmm/device/config.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

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

zx_status_t TestWithDevice::MakeStartInfo(
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

inspect::contrib::DiagnosticsData TestWithDevice::GetInspect(const std::string& selector,
                                                             const std::string& name) {
  fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  auto svc = sys::ServiceDirectory::CreateFromNamespace();
  svc->Connect(accessor.NewRequest());
  inspect::contrib::ArchiveReader reader(std::move(accessor), {selector});
  fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
  async::Executor executor(dispatcher());
  executor.schedule_task(reader.SnapshotInspectUntilPresent({name}).then(
      [&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>& rest) {
        result = std::move(rest);
      }));
  RunLoopWithTimeoutOrUntil([&] { return result.is_ok() || result.is_error(); }, zx::sec(10),
                            zx::msec(100));
  EXPECT_EQ(result.is_error(), false) << "Error was " << result.error();
  EXPECT_EQ(result.is_ok(), true) << "Result timed out";
  EXPECT_EQ(result.value().size(), 1ul) << "Expected only one component";
  return std::move(result.value()[0]);
}
