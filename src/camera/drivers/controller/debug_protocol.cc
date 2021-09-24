// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/debug_protocol.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/camera/drivers/controller/configs/product_config.h"

namespace camera {

DebugImpl::DebugImpl(zx_device_t* device,
                     fidl::InterfaceRequest<fuchsia::camera2::debug::Debug> request,
                     async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp,
                     fit::closure on_connection_closed)
    : binding_(this), isp_(isp) {
  binding_.set_error_handler(
      [occ = std::move(on_connection_closed)](zx_status_t /*status*/) { occ(); });
  binding_.Bind(std::move(request), dispatcher);
}

void DebugImpl::SetDefaultSensorMode(uint32_t mode, SetDefaultSensorModeCallback callback) {
  auto status = isp_.SetDefaultSensorMode(mode);
  callback(status);
}

}  // namespace camera
