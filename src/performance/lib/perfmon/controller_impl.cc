// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/lib/perfmon/controller_impl.h"

#include <fuchsia/perfmon/cpu/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/zx/status.h"
#include "src/performance/lib/perfmon/config_impl.h"
#include "src/performance/lib/perfmon/device_reader.h"
#include "src/performance/lib/perfmon/properties_impl.h"
#include "zircon/errors.h"

namespace perfmon::internal {

ControllerImpl::ControllerImpl(ControllerSyncPtr controller_ptr, uint32_t num_traces,
                               uint32_t buffer_size_in_pages, Config config)
    : controller_ptr_(std::move(controller_ptr)),
      num_traces_(num_traces),
      buffer_size_in_pages_(buffer_size_in_pages),
      config_(std::move(config)),
      weak_ptr_factory_(this) {}

ControllerImpl::~ControllerImpl() {
  auto status = Reset();
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Failed to Reset Controller: " << status.status_string();
  }
}

zx::status<> ControllerImpl::Start() {
  if (started_) {
    FX_LOGS(ERROR) << "already started";
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }

  auto status = Stage();
  if (status.is_error()) {
    return status;
  }

  ::fuchsia::perfmon::cpu::Controller_Start_Result result;
  status = zx::make_status(controller_ptr_->Start(&result));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Starting trace failed: status=" << status.status_string();
    return status;
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Starting trace failed: error=" << result.err();
    return zx::error(result.err());
  }

  started_ = true;
  return zx::ok();
}

zx::status<> ControllerImpl::Stop() {
  auto status = zx::make_status(controller_ptr_->Stop());
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Stopping trace failed: " << status.status_string();
    return status;
  }
  started_ = false;
  return zx::ok();
}

zx::status<> ControllerImpl::Stage() {
  FX_DCHECK(!started_);

  FidlPerfmonConfig fidl_config;
  internal::PerfmonToFidlConfig(config_, &fidl_config);

  ::fuchsia::perfmon::cpu::Controller_StageConfig_Result result;
  auto status = zx::make_status(controller_ptr_->StageConfig(fidl_config, &result));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Staging config failed: " << status.status_string();
    return status;
  }
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Staging config failed: error=" << result.err();
    return zx::error(result.err());
  }

  return zx::ok();
}

zx::status<> ControllerImpl::Terminate() {
  auto status = zx::make_status(controller_ptr_->Terminate());
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Terminating trace failed: " << status.status_string();
    return status;
  }
  started_ = false;
  return zx::ok();
}

zx::status<> ControllerImpl::Reset() {
  // Even if stopping fails, we still attempt to terminate to clean up.
  auto stop_status = Stop();
  auto terminate_status = Terminate();
  if (stop_status.is_error()) {
    return stop_status;
  }
  return terminate_status;
}

zx::status<zx::vmo> ControllerImpl::GetBufferHandle(const std::string& name, uint32_t trace_num) {
  uint32_t descriptor = trace_num;
  zx::vmo out_vmo;
  auto status = zx::make_status(controller_ptr_->GetBufferHandle(descriptor, &out_vmo));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Getting buffer handle failed: " << status.status_string();
    return status.take_error();
  }
  if (!out_vmo) {
    FX_LOGS(ERROR) << "Getting buffer handle failed: no handle returned";
    return zx::error(ZX_ERR_BAD_HANDLE);
  }
  return zx::ok(std::move(out_vmo));
}

zx::status<std::unique_ptr<Reader>> ControllerImpl::GetReader() {
  return DeviceReader::Create(weak_ptr_factory_.GetWeakPtr(), buffer_size_in_pages_);
}

}  // namespace perfmon::internal
