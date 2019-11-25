// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER_VIDEO_DEVICE_CLIENT_H_
#define SRC_CAMERA_CAMERA_MANAGER_VIDEO_DEVICE_CLIENT_H_

#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fuchsia/camera/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <list>

namespace camera {

// Client class for cameras and other video devices.  This class is intended to
// be used by the CameraManager, not applications.
class VideoDeviceClient : public fbl::RefCounted<VideoDeviceClient> {
  class VideoStream;

 public:
  using StartupCallback = ::fbl::Function<void(zx_status_t status)>;

  // The destructor signals all the connected drivers to close their
  // connections.
  ~VideoDeviceClient();

  // Create a VideoDeviceClient from a folder and filename.  This should be
  // by the CameraManagerImpl when it detects a new or existing device.
  static std::unique_ptr<VideoDeviceClient> Create(int dir_fd, const std::string &name);

  // Load all information to identify the device, as well as available formats.
  // Will call |callback| when everything is loaded.
  void Startup(StartupCallback callback);

  // Gets the device description that is published to the applications.
  fuchsia::camera::DeviceInfo GetDeviceInfo() const { return device_info_; }

  uint64_t id() const { return device_info_.camera_id; }

  // Get the formats that this device supports.  This is a local call.
  const std::vector<fuchsia::camera::VideoFormat> *GetFormats() const;

  // Attempt to establish a stream interface to the device.
  // If a connection is not possible, |stream| will not be connected,
  // and (because InterfaceRequest is move-only) the underlying channel
  // handle will be deleted.
  void CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                    fuchsia::camera::FrameRate frame_rate,
                    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                    zx::eventpair client_token);

 private:
  VideoDeviceClient() = default;

  // Called asynchronously by the driver in response to GetFormats.
  // Because GetFormats has a paginated response, OnGetFormatsResp may be
  // called multiple times.
  void OnGetFormatsResp(std::vector<fuchsia::camera::VideoFormat> formats,
                        uint32_t total_format_count, zx_status_t device_status);

  // Global counter to keep camera ids unique:
  static uint64_t camera_id_counter_;

  // All the formats this device supports.
  std::vector<fuchsia::camera::VideoFormat> formats_;

  // VideoStream is a book-keeping class to keep track of the streams that
  // this video device has created.  This class currently just tracks the
  // stream token, which can be used to kill the stream, but will TODO(CAM-15)
  // also contain the format and buffer information, so other applications
  // can attach to an active stream, and more detailed resource management
  // can be performed.
  class VideoStream {
   public:
    // Creates a VideoStream and initializes the eventpair and the async
    // waiter.  If anything fails, nullptr is returned.  Otherwise,
    // |driver_token| contains the peer to this class's internal token.
    static std::unique_ptr<VideoStream> Create(VideoDeviceClient *owner,
                                               zx::eventpair *driver_token);

   private:
    VideoStream() = default;
    zx::eventpair stream_token_;
    std::unique_ptr<async::Wait> stream_token_waiter_;
  };

  // VideoStream is the only one who should have access to RemoveActiveStream.
  friend class VideoStream;

  // Kill a specific stream, using a pointer to that stream, |stream_ptr|.
  // This function is only called when the stream token is closed, meaning that
  // the stream was shut down in the driver.
  void RemoveActiveStream(VideoStream *stream_ptr);

  // Device_info_ contains identifying information about the physical device.
  // It also contains the camera_id which uniquely identifies this instance.
  fuchsia::camera::DeviceInfo device_info_;

  // When we connect to a device, we query multiple bits of information.
  // This class keeps track of when we have gotten all the information we need
  // from the device driver, and calls back to the Camera Manager when the
  // device is ready to be used.
  class ReadyCallbackHandler {
   public:
    static constexpr int kFormatsReady = 1;
    static constexpr int kDeviceInfoReady = 2;
    // |callback| is called when either |timeout| is reached, or when
    // Signal is called with both values: kFormatsReady and kDeviceInfoReady.
    ReadyCallbackHandler(StartupCallback callback, zx::duration timeout)
        : callback_(std::move(callback)), timeout_task_([this]() {
            if (callback_) {
              callback_(ZX_ERR_TIMED_OUT);
              callback_ = nullptr;
            }
          }) {
      timeout_task_.PostDelayed(async_get_default_dispatcher(), timeout);
    }

    // Called to signal that a portion of the information has been received.
    void Signal(int signal);

   private:
    bool has_device_info_ = false;
    bool has_formats_ = false;
    StartupCallback callback_;
    // This timeout task will be cancel the task upon destruction.
    async::TaskClosure timeout_task_;
  };

  // When the driver is starting up, allow a reasonably large amount of time
  // to communicate driver information.  This timeout is mostly to make sure
  // that if the driver hangs, it will get dropped in a timely manner.
  // TODO(CAM-18): Remove this when we switch to a dynamic detection model.
  // Intentionally leaving timeout in deprecated class. See TODO(39822): delete CameraManager1
  static constexpr uint32_t kDriverStartupTimeoutSec = 10;
  std::unique_ptr<ReadyCallbackHandler> ready_callback_;

  // Depending on the device, multiple concurrent streams may be allowed:
  // The list of streams created for this device.
  std::list<std::unique_ptr<VideoStream>> active_streams_;

  // The connection to the device driver.
  fuchsia::camera::ControlPtr camera_control_;
};

}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER_VIDEO_DEVICE_CLIENT_H_
