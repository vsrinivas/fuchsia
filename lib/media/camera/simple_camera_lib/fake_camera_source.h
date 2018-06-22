// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_SOURCE_H_
#define GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_SOURCE_H_

#include <stdio.h>
#include <utility>

#include <fbl/vector.h>
#include <garnet/lib/media/camera/simple_camera_lib/buffer.h>
#include <garnet/lib/media/camera/simple_camera_lib/camera_interface_base.h>
#include <lib/async/cpp/task.h>
#include <lib/media/timeline/timeline_function.h>
#include <zircon/device/camera.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

namespace simple_camera {

// ColorSource steps through hue at a constant rate in HSV colorspace,
// with saturation and value remaining constant. An RGB color is written to
// a buffer provided.
class ColorSource {
 public:
  // Write the next color in the progression to the buffer.
  void WriteToBuffer(Buffer* buffer);

 private:
  void hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b);

  uint32_t frame_color_ = 0x80;
  static constexpr uint32_t kFrameColorInc = 0x01;
  static constexpr uint32_t kMaxFrameColor = 0x600;
};

// FakeCameraSource provides a "fake" implimentation of the
// Camera Interface without using hardware.  This call does not
// communicate over channels; It is run in the same process as the consumer.
// For more information on how to use the Camera interface, see
// camera_client.h.
class FakeCameraSource : public CameraInterfaceBase {
 public:
  FakeCameraSource() {}
  ~FakeCameraSource() {}

  // Open the interface.  Does nothing, and ignores the input argument.
  // This function is included for compatibility.
  zx_status_t Open(uint32_t dev_id, OnShutdownCallback shutdown_callback) {
    return ZX_OK;
  }
  void Close() {}

  // Get the supported formats for the camera.
  // get_formats_callback will be called in response.
  zx_status_t GetSupportedFormats(GetFormatCallback callback);

  // Set the format of the video stream.  This function
  // should only be called once in the setup of the stream.
  zx_status_t SetFormat(const camera_video_format_t& format,
                        SetFormatCallback callback);

  // Set the vmo that will be used to pass video information from the driver
  // to the consumer.  The entire vmo is assumed to be available.  The driver
  // will provide the offset into the vmo at which a frame has been written.
  // This function should only be called once in the setup of the stream.
  zx_status_t SetBuffer(const zx::vmo& vmo);

  // Signal the driver to start producing frames.  Upon each frame being
  // written, |frame_notify_callback| will be called. Start can be called
  // immediatly after calling SetBuffer.
  // This function should only be called once in the setup of the stream.
  zx_status_t Start(FrameNotifyCallback callback);

  // Release the lock on a video frame.  The data_offset corresponds to the
  // data_vb_offset field given in the camera_vb_frame_notify_t passed to
  // the frame_notify_callback.
  zx_status_t ReleaseFrame(uint64_t data_offset);

  // Signal the driver that it should stop producing frames.
  // Frame notifications may occur after calling this function.
  zx_status_t Stop();

 private:
  void SignalBufferFilled(uint32_t index);

  void PostNextCaptureTask();

  // Checks which buffer can be written to,
  // writes it then signals it ready
  // sleeps until next cycle
  void ProduceFrame();

  static constexpr uint32_t kMinNumberOfBuffers = 2;
  static constexpr uint32_t kFramesOfDelay = 2;
  ColorSource color_source_;
  uint64_t max_frame_size_ = 0;
  uint64_t frame_count_ = 0;
  media::TimelineFunction frame_to_timestamp_;
  camera_video_format_t format_;
  std::vector<std::unique_ptr<Buffer>> buffers_;
  async::TaskClosureMethod<FakeCameraSource,
                           &FakeCameraSource::ProduceFrame> task_{this};
  FrameNotifyCallback notify_callback_;
};

}  // namespace simple_camera

#endif  // GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FAKE_CAMERA_SOURCE_H_
