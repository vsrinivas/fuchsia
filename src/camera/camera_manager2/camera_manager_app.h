// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_CAMERA_MANAGER_APP_H_
#define SRC_CAMERA_CAMERA_MANAGER2_CAMERA_MANAGER_APP_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <list>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <src/camera/camera_manager2/camera_plug_detector.h>
#include <src/camera/camera_manager2/video_device_client.h>
#include <src/lib/fsl/io/device_watcher.h>

namespace camera {
// Keeps track of the cameras and other video input devices that are plugged in,
// making that information available to applications.
// Also, keeps track of the connections to a device, ensuring that applications
// do not open more connections than the device can support.
class CameraManagerApp : public fuchsia::camera2::Manager {
 public:
  // In addition to shutting down the camera::Manager service, this destructor
  // will cancel all video streams, and close all client connections.
  ~CameraManagerApp() override;

  CameraManagerApp(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {}
  static std::unique_ptr<CameraManagerApp> Create(std::unique_ptr<sys::ComponentContext> context);

  // Connect to a camera stream:
  // |camera_id| Refers to a specific camera_id from a CameraInfo that has been
  // advertised by OnCameraAvailable.
  // |constraints| contains a set of constraints on the requested stream.  The Camera
  // Manager will attempt to find a stream that meets the constraints.  If multiple
  // streams match, one of the matching streams will be connected.
  // |token| refers to a Sysmem buffer allocation that will be used to pass images using
  // the Stream protocol.  The Camera Manager will apply a BufferCollectionContraints
  // related to the image format(s), so the client does not need to apply any
  // ImageFormatConstraints.
  // Sync is assumed to have been called on |token| before it is passed to
  // ConnectToStream.
  // Since |constraints| may not dictate a specific format, the initial format of images
  // on the stream is indicated on the response.
  // The connection is considered to be successful once a response has been given, unless
  // |stream| is closed.
  void ConnectToStream(int32_t camera_id, fuchsia::camera2::StreamConstraints constraints,
                       fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                       fidl::InterfaceRequest<fuchsia::camera2::Stream> client_request,
                       fuchsia::camera2::Manager::ConnectToStreamCallback callback) override;

  // Provides flow control.  The client must acknowledge every event before
  // more events can be sent.
  // TODO(35252): implement the device event notifications, and populate this
  // function.
  void AcknowledgeDeviceEvent() override {}

 private:
  // Called when a device is enumerated, or when this class starts, and
  // discovers all the current devices in the system.
  void OnDeviceFound(fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);

  // Helper function.  Gets the device with id |camera_id| if it exists.
  VideoDeviceClient* GetActiveDevice(int32_t camera_id);

  std::list<std::unique_ptr<VideoDeviceClient>> devices_;

  CameraPlugDetector plug_detector_;

  // Counter to keep track of device id's we have give out so far.
  int32_t device_id_counter_ = 0;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::camera2::Manager> bindings_;
};

}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER2_CAMERA_MANAGER_APP_H_
