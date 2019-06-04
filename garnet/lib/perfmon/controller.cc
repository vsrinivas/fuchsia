// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/controller.h"

#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fuchsia/perfmon/cpu/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/perfmon/config_impl.h"
#include "garnet/lib/perfmon/controller_impl.h"
#include "garnet/lib/perfmon/device_reader.h"
#include "garnet/lib/perfmon/properties_impl.h"

namespace perfmon {

// Shorten some long FIDL names.
using FidlPerfmonAllocation = ::fuchsia::perfmon::cpu::Allocation;

const char kPerfMonDev[] = "/dev/sys/cpu-trace/perfmon";

static uint32_t RoundUpToPages(uint32_t value) {
  uint32_t size = fbl::round_up(value, Controller::kPageSize);
  FXL_DCHECK(size & ~(Controller::kPageSize - 1));
  return size >> Controller::kLog2PageSize;
}

static uint32_t GetBufferSizeInPages(CollectionMode mode,
                                     uint32_t requested_size_in_pages) {
  switch (mode) {
  case CollectionMode::kSample:
    return requested_size_in_pages;
  case CollectionMode::kTally: {
    // For tally mode we just need something large enough to hold
    // the header + records for each event.
    unsigned num_events = kMaxNumEvents;
    uint32_t size = (sizeof(BufferHeader) +
                     num_events * sizeof(ValueRecord));
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

bool Controller::GetProperties(Properties* props) {
  ::fuchsia::perfmon::cpu::ControllerSyncPtr controller_ptr;
  zx_status_t status = fdio_service_connect(
      kPerfMonDev, controller_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error connecting to " << kPerfMonDev << ": "
                   << status;
    return false;
  }

  FidlPerfmonProperties properties;
  status = controller_ptr->GetProperties(&properties);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get properties: " << status;
    return false;
  }

  internal::FidlToPerfmonProperties(properties, props);
  return true;
}

static bool Initialize(
    ::fuchsia::perfmon::cpu::ControllerSyncPtr* controller_ptr,
    uint32_t num_traces, uint32_t buffer_size_in_pages) {
  FidlPerfmonAllocation allocation;
  allocation.num_buffers = num_traces;
  allocation.buffer_size_in_pages = buffer_size_in_pages;
  FXL_VLOG(2) << fxl::StringPrintf("num_buffers=%u, buffer_size_in_pages=0x%x",
                                   num_traces, buffer_size_in_pages);

  ::fuchsia::perfmon::cpu::Controller_Initialize_Result result;
  zx_status_t status = (*controller_ptr)->Initialize(allocation, &result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Initialize failed: status=" << status;
    return false;
  }
  if (result.is_err() && result.err() != ZX_ERR_BAD_STATE) {
    FXL_LOG(ERROR) << "Initialize failed: error=" << result.err();
    return false;
  }

  // TODO(dje): If we get BAD_STATE, a previous run may have crashed without
  // resetting the device. The device doesn't reset itself on close yet.
  if (result.is_err()) {
    FXL_DCHECK(result.err() == ZX_ERR_BAD_STATE);
    FXL_VLOG(2) << "Got BAD_STATE trying to initialize a trace,"
                << " resetting device and trying again";
    status = (*controller_ptr)->Stop();
    if (status != ZX_OK) {
      FXL_VLOG(2) << "Stopping device failed: status=" << status;
      return false;
    }
    status = (*controller_ptr)->Terminate();
    if (status != ZX_OK) {
      FXL_VLOG(2) << "Terminating previous trace failed: status=" << status;
      return false;
    }
    status = (*controller_ptr)->Initialize(allocation, &result);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Initialize try #2 failed: status=" << status;
      return false;
    }
    if (result.is_err()) {
      FXL_LOG(ERROR) << "Initialize try #2 failed: error=" << result.err();
      return false;
    }
    FXL_VLOG(2) << "Second Initialize attempt succeeded";
  }

  return true;
}

bool Controller::Create(uint32_t buffer_size_in_pages, const Config config,
                        std::unique_ptr<Controller>* out_controller) {
  if (buffer_size_in_pages > kMaxBufferSizeInPages) {
    FXL_LOG(ERROR) << "Buffer size is too large, max " << kMaxBufferSizeInPages
                   << " pages";
    return false;
  }

  ::fuchsia::perfmon::cpu::ControllerSyncPtr controller_ptr;
  zx_status_t status = fdio_service_connect(
      kPerfMonDev, controller_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error connecting to " << kPerfMonDev << ": "
                   << status;
    return false;
  }

  CollectionMode mode = config.GetMode();
  uint32_t num_traces = zx_system_get_num_cpus();
  // For "tally" mode we only need a small fixed amount, so toss what the
  // caller provided and use our own value.
  uint32_t actual_buffer_size_in_pages =
      GetBufferSizeInPages(mode, buffer_size_in_pages);

  if (!Initialize(&controller_ptr, num_traces,
                  actual_buffer_size_in_pages)) {
    return false;
  }

  out_controller->reset(new internal::ControllerImpl(
      std::move(controller_ptr), num_traces, buffer_size_in_pages,
      std::move(config)));
  return true;
}

}  // namespace perfmon
