// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/limbo_client.h"

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace exception {

LimboClient::LimboClient(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)) {}

zx_status_t LimboClient::Init() {
  FXL_DCHECK(services_);
  if (zx_status_t status = services_->Connect(connection_.NewRequest()); status != ZX_OK)
    return status;

  // Check for active.
  bool active = false;
  if (zx_status_t status = connection_->WatchActive(&active); status != ZX_OK)
    return status;

  active_ = active;

  return ZX_OK;
}

zx_status_t LimboClient::GetFilters(std::vector<std::string>* filters) {
  if (!connection_)
    return ZX_ERR_UNAVAILABLE;
  return connection_->GetFilters(filters);
}

zx_status_t LimboClient::AppendFilters(const std::vector<std::string>& filters) {
  if (!connection_)
    return ZX_ERR_UNAVAILABLE;

  ProcessLimbo_AppendFilters_Result result;
  if (zx_status_t status = connection_->AppendFilters(filters, &result); status != ZX_OK)
    return status;

  if (result.err())
    return result.err();
  return ZX_OK;
}

}  // namespace exception
}  // namespace fuchsia
