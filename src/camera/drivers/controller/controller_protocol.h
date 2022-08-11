// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/fidl/cpp/binding.h>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/camera/drivers/controller/pipeline_manager.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"

namespace camera {

class ControllerImpl : public fuchsia::camera2::hal::Controller {
 public:
  ControllerImpl(async_dispatcher_t* dispatcher, const ddk::SysmemProtocolClient& sysmem,
                 const ddk::IspProtocolClient& isp, const ddk::GdcProtocolClient& gdc,
                 const ddk::Ge2dProtocolClient& ge2d, LoadFirmwareCallback load_firmware);
  void Connect(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request);

 private:
  // fuchsia.camera2.hal.Controller implementation
  void EnableStreaming() override;
  void DisableStreaming() override;
  void GetNextConfig(GetNextConfigCallback callback) override;
  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override;
  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
  InternalConfigs internal_configs_;
  PipelineManager pipeline_manager_;
  uint32_t pipeline_config_index_ = -1;
  using RequestQueue =
      std::queue<std::tuple<uint32_t, uint32_t, fidl::InterfaceRequest<fuchsia::camera2::Stream>>>;
  std::optional<RequestQueue> requests_;
  uint32_t config_count_ = 0;
  std::unique_ptr<ProductConfig> product_config_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
