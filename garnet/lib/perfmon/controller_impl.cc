// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/controller_impl.h"

#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/config_impl.h"
#include "garnet/lib/perfmon/device_reader.h"
#include "garnet/lib/perfmon/properties_impl.h"

namespace perfmon {
namespace internal {

ControllerImpl::ControllerImpl(fxl::UniqueFD fd, uint32_t num_traces,
                               uint32_t buffer_size_in_pages, Config config)
    : fd_(std::move(fd)),
      num_traces_(num_traces),
      buffer_size_in_pages_(buffer_size_in_pages),
      config_(std::move(config)),
      weak_ptr_factory_(this) {
}

ControllerImpl::~ControllerImpl() {
  Reset();
}

bool ControllerImpl::Start() {
  if (started_) {
    FXL_LOG(ERROR) << "already started";
    return false;
  }

  if (!Stage()) {
    return false;
  }

  auto status = ioctl_perfmon_start(fd_.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_perfmon_start failed: status=" << status;
  } else {
    started_ = true;
  }
  return status == ZX_OK;
}

void ControllerImpl::Stop() {
  auto status = ioctl_perfmon_stop(fd_.get());
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (!started_ && status == ZX_ERR_BAD_STATE) {
      ;  // dont report an error in this case
    } else {
      FXL_LOG(ERROR) << "ioctl_perfmon_stop failed: status=" << status;
    }
  } else {
    started_ = false;
  }
}

bool ControllerImpl::Stage() {
  FXL_DCHECK(!started_);

  perfmon_ioctl_config_t ioctl_config;
  internal::PerfmonToIoctlConfig(config_, &ioctl_config);

  auto status = ioctl_perfmon_stage_config(fd_.get(), &ioctl_config);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_perfmon_stage_config failed: status=" << status;
  }
  return status == ZX_OK;
}

void ControllerImpl::Free() {
  auto status = ioctl_perfmon_free_trace(fd_.get());
  if (status != ZX_OK) {
    // This can get called while tracing is currently stopped.
    if (!started_ && status == ZX_ERR_BAD_STATE) {
      ;  // dont report an error in this case
    } else {
      FXL_LOG(ERROR) << "ioctl_perfmon_free_trace failed: status=" << status;
    }
  }
}

void ControllerImpl::Reset() {
  Stop();
  Free();
}

bool ControllerImpl::GetBufferHandle(const std::string& name,
                                     uint32_t trace_num, zx::vmo* out_vmo) {
  ioctl_perfmon_buffer_handle_req_t req;
  req.descriptor = trace_num;
  auto status = ioctl_perfmon_get_buffer_handle(
      fd_.get(), &req, out_vmo->reset_and_get_address());
  if (status < 0) {
    FXL_LOG(ERROR) << name << ": ioctl_perfmon_get_buffer_handle failed: "
                   << status;
    return false;
  }
  return true;
}

std::unique_ptr<Reader> ControllerImpl::GetReader() {
  std::unique_ptr<Reader> reader;
  if (DeviceReader::Create(weak_ptr_factory_.GetWeakPtr(),
                           buffer_size_in_pages_, &reader)) {
    return reader;
  }
  return nullptr;
}

}  // namespace internal
}  // namespace perfmon
