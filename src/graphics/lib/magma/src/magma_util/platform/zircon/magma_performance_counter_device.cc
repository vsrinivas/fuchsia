// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_performance_counter_device.h"

#include <ddktl/fidl.h>

#include "magma_util/macros.h"

namespace magma {

MagmaPerformanceCounterDevice::MagmaPerformanceCounterDevice(zx_device_t* parent)
    : DdkPerfCountDeviceType(parent) {}

zx_koid_t MagmaPerformanceCounterDevice::GetEventKoid() {
  ZX_DEBUG_ASSERT(!event_);
  zx_status_t status = zx::event::create(0, &event_);
  if (status != ZX_OK)
    return 0;
  zx_info_handle_basic_t info;
  if (event_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK)
    return 0;
  return info.koid;
}

zx_status_t MagmaPerformanceCounterDevice::Bind(
    std::unique_ptr<MagmaPerformanceCounterDevice>* device) {
  zx_status_t status = DdkAdd("gpu-performance-counters");
  if (status == ZX_OK) {
    // DDK took ownership of the device.
    device->release();
  }
  return DRET(status);
}

zx_status_t MagmaPerformanceCounterDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::gpu::magma::PerformanceCounterAccess::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void MagmaPerformanceCounterDevice::GetPerformanceCountToken(
    GetPerformanceCountTokenCompleter::Sync& completer) {
  zx::event event_duplicate;
  zx_status_t status = event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_duplicate);
  if (status != ZX_OK) {
    completer.Close(status);
  } else {
    completer.Reply(std::move(event_duplicate));
  }
}

// static
bool MagmaPerformanceCounterDevice::AddDevice(zx_device_t* parent, zx_koid_t* koid_out) {
  auto perf_count_access = std::make_unique<magma::MagmaPerformanceCounterDevice>(parent);
  zx_koid_t koid = perf_count_access->GetEventKoid();
  if (!koid)
    return DRETF(false, "Failed to get event koid");
  zx_status_t status = perf_count_access->Bind(&perf_count_access);
  if (status != ZX_OK)
    return DRETF(false, "Failed to bind device");
  *koid_out = koid;
  return true;
}

}  // namespace magma
