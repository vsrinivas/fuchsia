// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service.h"

namespace btlib {
namespace gatt {

RemoteService::RemoteService(const ServiceData& service_data,
                             fxl::WeakPtr<Client> client,
                             async_t* gatt_dispatcher)
    : service_data_(service_data),
      client_(client),
      gatt_dispatcher_(gatt_dispatcher),
      shut_down_(false) {
  FXL_DCHECK(client_);
  FXL_DCHECK(gatt_dispatcher_);
}

void RemoteService::ShutDown() {
  std::lock_guard<std::mutex> lock(mtx_);

  FXL_DCHECK(!shut_down_)
      << "gatt: RemoteService::ShutDown() called more than once!";

  shut_down_ = true;
  if (closed_callback_) {
    closed_callback_();
  }
}

bool RemoteService::SetClosedCallback(fxl::Closure callback) {
  std::lock_guard<std::mutex> lock(mtx_);

  if (shut_down_)
    return false;

  closed_callback_ = std::move(callback);
  return true;
}

}  // namespace gatt
}  // namespace btlib
