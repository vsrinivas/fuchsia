// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/debug_protocol.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace camera {

DebugImpl::DebugImpl(async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp)
    : dispatcher_(dispatcher), binding_(this), isp_(isp) {}

void DebugImpl::Connect(fidl::InterfaceRequest<fuchsia::camera2::debug::Debug> request) {
  // Intentionally replace any existing connection.
  binding_.Bind(std::move(request), dispatcher_);
}

void DebugImpl::SetDefaultSensorMode(uint32_t mode, SetDefaultSensorModeCallback callback) {
  auto status = isp_.SetDefaultSensorMode(mode);
  callback(status);
}

}  // namespace camera
