// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

#include <zircon/status.h>

using namespace fuchsia::exception;

namespace debug_agent {

LimboProvider::LimboProvider(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)) {}
LimboProvider::~LimboProvider() = default;

zx_status_t LimboProvider::ListProcessesOnLimbo(std::vector<ProcessExceptionMetadata>* out) {
  // We connect synchronously to the limbo service.
  ProcessLimboSyncPtr process_limbo;
  zx_status_t status = services_->Connect(process_limbo.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Could not connect to process limbo: " << zx_status_get_string(status);
    return status;
  }

  return process_limbo->ListProcessesWaitingOnException(out);
}

zx_status_t LimboProvider::RetrieveException(uint64_t process_koid,
                                             fuchsia::exception::ProcessException* out) {
  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK) {
    FXL_LOG(WARNING) << "Could not connect to process limbo: " << zx_status_get_string(status);
    return status;
  }

  ProcessLimbo_RetrieveException_Result result = {};
  if (zx_status_t status = process_limbo->RetrieveException(process_koid, &result);
      status != ZX_OK) {
    FXL_LOG(WARNING) << "Could not obtain exception from limbo: " << zx_status_get_string(status);
    return status;
  }

  if (result.is_err()) {
    zx_status_t status = result.err();
    FXL_LOG(WARNING) << "Error retreiving exception from limbo: " << zx_status_get_string(status);
    return status;
  }

  *out = std::move(result.response().ResultValue_());
  return ZX_OK;
}

}  // namespace debug_agent
