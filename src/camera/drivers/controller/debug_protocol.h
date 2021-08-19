// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_DEBUG_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_DEBUG_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/debug/cpp/fidl.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <lib/fidl/cpp/binding.h>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/camera/drivers/controller/isp_stream_protocol.h"
#include "src/camera/drivers/controller/pipeline_manager.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"

namespace camera {

class DebugImpl : public fuchsia::camera2::debug::Debug {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(DebugImpl);
  DebugImpl(zx_device_t* device, fidl::InterfaceRequest<fuchsia::camera2::debug::Debug> request,
            async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp,
            fit::closure on_connection_closed);
  void SetDefaultSensorMode(uint32_t mode, SetDefaultSensorModeCallback callback) override;

  void Shutdown(){};

 private:
  fidl::Binding<fuchsia::camera2::debug::Debug> binding_;
  const ddk::IspProtocolClient& isp_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_DEBUG_PROTOCOL_H_
