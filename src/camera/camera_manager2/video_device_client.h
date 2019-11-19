// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_VIDEO_DEVICE_CLIENT_H_
#define SRC_CAMERA_CAMERA_MANAGER2_VIDEO_DEVICE_CLIENT_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <list>

namespace camera {

// Representation of a client of the CameraManager, used internally by the CameraManager.
class VideoDeviceClient {
 public:
  using StartupCallback = ::fit::function<void(zx_status_t status)>;

  // Create a VideoDeviceClient to handle the connection to a camera HAL.
  // The VideoDeviceClient takes ownership of |controller|.
  // This call makes syncronous calls to |controller|, so this call can be
  // blocked by a misbehaving HAL implementation.
  static std::unique_ptr<VideoDeviceClient> Create(
      fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);

  // Gets the device description that is published to the applications.
  const fuchsia::camera2::DeviceInfo& GetDeviceInfo() const { return device_info_; }

  zx_status_t CreateStream(
      uint32_t config_index, uint32_t stream_type, uint32_t image_format_index,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> sysmem_collection,
      ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream);
  void set_id(int32_t id) { device_id_ = id; }
  int32_t id() const { return device_id_; }
  bool muted() const { return muted_; }

  const std::vector<fuchsia::camera2::hal::Config>& configs() { return configs_; }

  // Matches first config with the same stream_type.
  // Returns ZX_ERR_NO_RESOURCES if none found.
  // Returns ZX_ERR_INVALID_ARGS if contraints does not have a stream type.
  zx_status_t MatchConstraints(const fuchsia::camera2::StreamConstraints& constraints,
                               uint32_t* config_index, uint32_t* stream_type);

 private:
  struct Stream {
    uint32_t config_index, stream_type;
    fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection;
  };

  std::list<Stream> open_streams_;

  VideoDeviceClient() = default;
  // Get the device info and configs when the device is discovered.
  zx_status_t GetInitialInfo();
  fuchsia::camera2::DeviceInfo device_info_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
  int32_t device_id_;
  bool muted_ = false;
  // The connection to the device driver.
  fuchsia::camera2::hal::ControllerSyncPtr camera_control_;
};

}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER2_VIDEO_DEVICE_CLIENT_H_
