// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_
#define SRC_CAMERA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <list>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/ref_counted.h>

#include "src/camera/camera_manager/video_device_client.h"
#include "src/lib/fsl/io/device_watcher.h"

namespace camera {
// Implements camera::Manager FIDL service.  Keeps track of the cameras and
// other video input devices that are plugged in, making that information
// available to applications.  Also, keeps track of the connections to a
// device, ensuring that applications do not open more connections than the
// device can support.
class CameraManagerImpl : public fuchsia::camera::Manager {
 public:
  // In addition to shuting down the camera::Manager service, this destructor
  // will attempt to cancel all video streams, even if they are connected
  // directly from the device driver to the application.
  ~CameraManagerImpl() override;

  // This initialization is passed the async::Loop because it will be stepping
  // the loop forward until all the devices are enumerated. |loop| should be
  // the async loop associated with the default dispatcher.
  // This constructor will not return until all existing camera devices have
  // been enumerated and set up.
  explicit CameraManagerImpl(async::Loop* loop);

  // Returns a list of all the video devices that are currently plugged in
  // and enumerated.  The camera_id field of the DeviceInfo is used to specify
  // a device in GetFormats, GetStream and GetStreamAndBufferCollection.
  void GetDevices(GetDevicesCallback callback) override;

  // Get all the available formats for a camera.
  // TODO(CAM-17): Add pagination to support cameras with over 16 formats.
  void GetFormats(uint64_t camera_id, uint32_t index, GetFormatsCallback callback) override;

  // Establish a camera stream connection, which allows camera image data
  // to be passed over a set of buffers.
  void CreateStream(fuchsia::camera::VideoStream request,
                    fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                    zx::eventpair client_token) override;

 private:
  // Called when a device is enumerated, or when this class starts, and
  // discovers all the current devices in the system.
  void OnDeviceFound(int dir_fd, const std::string& filename);

  void AddDevice(std::unique_ptr<VideoDeviceClient> device);

  // Called by the device once it finishes initializing.
  void OnDeviceStartupComplete(uint64_t camera_id, zx_status_t status);

  std::list<std::unique_ptr<VideoDeviceClient>> active_devices_;
  // List of not-yet-activated cameras, waiting to get information from
  // the driver.
  std::list<std::unique_ptr<VideoDeviceClient>> inactive_devices_;

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Manager> bindings_;
};

}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER_CAMERA_MANAGER_IMPL_H_
