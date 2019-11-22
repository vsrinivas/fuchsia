// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/limbo_client.h"

#include <fuchsia/exception/cpp/fidl.h>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace exception {

LimboClient::LimboClient(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)) {}

zx_status_t LimboClient::Init() {
  FXL_DCHECK(services_);

  ProcessLimboSyncPtr process_limbo;
  if (zx_status_t status = services_->Connect(process_limbo.NewRequest()); status != ZX_OK)
    return status;

  // Check for active.
  bool active = false;
  if (zx_status_t status = process_limbo->WatchActive(&active); status != ZX_OK)
    return status;

  active_ = active;

  return ZX_OK;
}

}  // namespace exception
}  // namespace fuchsia
