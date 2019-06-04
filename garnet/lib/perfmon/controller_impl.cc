// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/controller_impl.h"

#include <fuchsia/perfmon/cpu/cpp/fidl.h>
#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/config_impl.h"
#include "garnet/lib/perfmon/device_reader.h"
#include "garnet/lib/perfmon/properties_impl.h"

namespace perfmon {
namespace internal {

ControllerImpl::ControllerImpl(ControllerSyncPtr controller_ptr,
                               uint32_t num_traces,
                               uint32_t buffer_size_in_pages, Config config)
    : controller_ptr_(std::move(controller_ptr)),
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

  ::fuchsia::perfmon::cpu::Controller_Start_Result result;
  zx_status_t status = controller_ptr_->Start(&result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Starting trace failed: status=" << status;
    return false;
  }
  if (result.is_err()) {
    FXL_LOG(ERROR) << "Starting trace failed: error=" << result.err();
    return false;
  }

  started_ = true;
  return true;
}

void ControllerImpl::Stop() {
  zx_status_t status = controller_ptr_->Stop();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Stopping trace failed: status=" << status;
  } else {
    started_ = false;
  }
}

bool ControllerImpl::Stage() {
  FXL_DCHECK(!started_);

  FidlPerfmonConfig fidl_config;
  internal::PerfmonToFidlConfig(config_, &fidl_config);

  ::fuchsia::perfmon::cpu::Controller_StageConfig_Result result;
  zx_status_t status = controller_ptr_->StageConfig(fidl_config, &result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Staging config failed: status=" << status;
    return false;
  }
  if (result.is_err()) {
    FXL_LOG(ERROR) << "Staging config failed: error=" << result.err();
    return false;
  }

  return true;
}

void ControllerImpl::Terminate() {
  zx_status_t status = controller_ptr_->Terminate();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Terminating trace failed: status=" << status;
  } else {
    started_ = false;
  }
}

void ControllerImpl::Reset() {
  Stop();
  Terminate();
}

bool ControllerImpl::GetBufferHandle(const std::string& name,
                                     uint32_t trace_num, zx::vmo* out_vmo) {
  uint32_t descriptor = trace_num;
  zx_status_t status = controller_ptr_->GetBufferHandle(descriptor, out_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Getting buffer handle failed: status="
                   << status;
    return false;
  }
  if (!*out_vmo) {
    FXL_LOG(ERROR) << "Getting buffer handle failed: no handle returned";
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
