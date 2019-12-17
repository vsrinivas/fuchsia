// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/binding.h>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/isp.h>

#include "configs/sherlock/internal-config.h"
#include "isp_stream_protocol.h"
#include "pipeline_manager.h"
#include "processing_node.h"
#include "stream_pipeline_info.h"

namespace camera {

namespace {
const char* kCameraVendorName = "Google Inc.";
const char* kCameraProductName = "Fuchsia Sherlock Camera";
}  // namespace

class ControllerImpl : public fuchsia::camera2::hal::Controller {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ControllerImpl);
  ControllerImpl(zx_device_t* device,
                 fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
                 async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp,
                 const ddk::GdcProtocolClient& gdc, fit::closure on_connection_closed,
                 fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator);

  explicit ControllerImpl(zx_device_t* device, const ddk::IspProtocolClient& isp,
                          const ddk::GdcProtocolClient& gdc,
                          fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : binding_(nullptr), pipeline_manager_(device, isp, gdc, std::move(sysmem_allocator)) {}

  zx_status_t GetInternalConfiguration(uint32_t config_index, InternalConfigInfo** internal_config);
  InternalConfigNode* GetStreamConfigNode(InternalConfigInfo* internal_config,
                                          fuchsia::camera2::CameraStreamType stream_config_type);

  void PopulateConfigurations() { configs_ = SherlockConfigs(); }

 private:
  // Device FIDL implementation

  // Get a list of all available configurations which the camera driver supports.
  void GetConfigs(GetConfigsCallback callback) override;

  // Set a particular configuration and create the requested stream.
  // |config_index| : Configuration index from the vector which needs to be applied.
  // |stream_index| : Stream index
  // |buffer_collection| : Buffer collections for the stream.
  // |stream| : Stream channel for the stream requested
  // |image_format_index| : Image format index which needs to be set up upon creation.
  // If there is already an active configuration which is different than the one
  // which is requested to be set, then the HAL will be closing all existing streams
  // and honor this new setup call.
  // If the new stream requested is already part of the existing running configuration
  // the HAL will just be creating this new stream while the other stream still exists as is.
  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override;

  // Enable/Disable Streaming
  void EnableStreaming() override;

  void DisableStreaming() override;

  void GetDeviceInfo(GetDeviceInfoCallback callback) override;

  std::vector<fuchsia::camera2::hal::Config> SherlockConfigs();

  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
  InternalConfigs internal_configs_;
  PipelineManager pipeline_manager_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROTOCOL_H_
