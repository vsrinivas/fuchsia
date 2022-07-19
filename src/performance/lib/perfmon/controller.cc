// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/lib/perfmon/controller.h"

#include <fuchsia/perfmon/cpu/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>

#include <memory>

#include <fbl/algorithm.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/performance/lib/perfmon/config_impl.h"
#include "src/performance/lib/perfmon/controller_impl.h"
#include "src/performance/lib/perfmon/device_reader.h"
#include "src/performance/lib/perfmon/properties_impl.h"
#include "zircon/system/public/zircon/errors.h"

namespace perfmon {

// Shorten some long FIDL names.
using FidlPerfmonAllocation = ::fuchsia::perfmon::cpu::Allocation;

const char kPerfMonDev[] = "/dev/sys/cpu-trace/perfmon";

static uint32_t RoundUpToPages(uint32_t value) {
  uint32_t size = fbl::round_up(value, Controller::kPageSize);
  FX_DCHECK(size & ~(Controller::kPageSize - 1));
  return size >> Controller::kLog2PageSize;
}

static uint32_t GetBufferSizeInPages(CollectionMode mode, uint32_t requested_size_in_pages) {
  switch (mode) {
    case CollectionMode::kSample:
      return requested_size_in_pages;
    case CollectionMode::kTally: {
      // For tally mode we just need something large enough to hold
      // the header + records for each event.
      unsigned num_events = kMaxNumEvents;
      uint32_t size = (sizeof(BufferHeader) + num_events * sizeof(ValueRecord));
      return RoundUpToPages(size);
    }
    default:
      __UNREACHABLE;
  }
}

bool Controller::IsSupported() {
  // The device path isn't present if it's not supported.
  struct stat stat_buffer;
  if (stat(kPerfMonDev, &stat_buffer) != 0)
    return false;
  return S_ISCHR(stat_buffer.st_mode);
}

zx::status<Properties> Controller::GetProperties() {
  ::fuchsia::perfmon::cpu::ControllerSyncPtr controller_ptr;
  zx_status_t status =
      fdio_service_connect(kPerfMonDev, controller_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to " << kPerfMonDev << ": " << status;
    return zx::error(status);
  }

  FidlPerfmonProperties properties;
  status = controller_ptr->GetProperties(&properties);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get properties: " << status;
    return zx::error(status);
  }

  return zx::ok(internal::FidlToPerfmonProperties(properties));
}

static zx::status<> Initialize(::fuchsia::perfmon::cpu::ControllerSyncPtr* controller_ptr,
                               uint32_t num_traces, uint32_t buffer_size_in_pages) {
  FidlPerfmonAllocation allocation;
  allocation.num_buffers = num_traces;
  allocation.buffer_size_in_pages = buffer_size_in_pages;
  FX_VLOGS(2) << fxl::StringPrintf("num_buffers=%u, buffer_size_in_pages=0x%x", num_traces,
                                   buffer_size_in_pages);

  ::fuchsia::perfmon::cpu::Controller_Initialize_Result result;
  zx_status_t status = (*controller_ptr)->Initialize(allocation, &result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Initialize failed: status=" << status;
    return zx::error(status);
  }
  if (result.is_err() && result.err() != ZX_ERR_BAD_STATE) {
    FX_LOGS(ERROR) << "Initialize failed: error=" << result.err();
    return zx::error(result.err());
  }

  // TODO(dje): If we get BAD_STATE, a previous run may have crashed without
  // resetting the device. The device doesn't reset itself on close yet.
  if (result.is_err()) {
    FX_DCHECK(result.err() == ZX_ERR_BAD_STATE);
    FX_VLOGS(2) << "Got BAD_STATE trying to initialize a trace,"
                << " resetting device and trying again";
    status = (*controller_ptr)->Stop();
    if (status != ZX_OK) {
      FX_VLOGS(2) << "Stopping device failed: status=" << status;
      return zx::error(status);
    }
    status = (*controller_ptr)->Terminate();
    if (status != ZX_OK) {
      FX_VLOGS(2) << "Terminating previous trace failed: status=" << status;
      return zx::error(status);
    }
    status = (*controller_ptr)->Initialize(allocation, &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Initialize try #2 failed: status=" << status;
      return zx::error(status);
    }
    if (result.is_err()) {
      FX_LOGS(ERROR) << "Initialize try #2 failed: error=" << result.err();
      return zx::error(status);
    }
    FX_VLOGS(2) << "Second Initialize attempt succeeded";
  }

  return zx::ok();
}

zx::status<std::unique_ptr<Controller>> Controller::Create(uint32_t buffer_size_in_pages,
                                                           Config config) {
  if (buffer_size_in_pages > kMaxBufferSizeInPages) {
    FX_LOGS(ERROR) << "Buffer size is too large, max " << kMaxBufferSizeInPages << " pages";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  ::fuchsia::perfmon::cpu::ControllerSyncPtr controller_ptr;
  auto status = zx::make_status(
      fdio_service_connect(kPerfMonDev, controller_ptr.NewRequest().TakeChannel().release()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Error connecting to " << kPerfMonDev << ": " << status.status_string();
    return status.take_error();
  }

  CollectionMode mode = config.GetMode();
  uint32_t num_traces = zx_system_get_num_cpus();
  // For "tally" mode we only need a small fixed amount, so toss what the
  // caller provided and use our own value.
  uint32_t actual_buffer_size_in_pages = GetBufferSizeInPages(mode, buffer_size_in_pages);

  auto init_status = Initialize(&controller_ptr, num_traces, actual_buffer_size_in_pages);
  if (init_status.is_error()) {
    return init_status.take_error();
  }

  return zx::ok(std::make_unique<internal::ControllerImpl>(
      std::move(controller_ptr), num_traces, buffer_size_in_pages, std::move(config)));
}

}  // namespace perfmon
