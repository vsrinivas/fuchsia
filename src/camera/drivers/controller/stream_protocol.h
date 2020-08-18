// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/compiler.h>

#include <ddktl/protocol/isp.h>

#include "src/lib/fxl/synchronization/thread_checker.h"

namespace camera {

class ProcessNode;

// Server-side implementation of a stream.
class StreamImpl : public fuchsia::camera2::Stream {
 public:
  explicit StreamImpl(ProcessNode* output_node);

  // Binds a channel to the stream.
  // Args:
  //   |channel|: the channel to bind
  //   |disconnect_handler|: called when the client on |channel| disconnects
  zx_status_t Attach(zx::channel channel, fit::function<void(void)> disconnect_handler);

  void FrameReady(const frame_available_info_t* info);

  // | fuchsia::camera2::Stream |
  void Start() override;
  void Stop() override;
  void ReleaseFrame(uint32_t buffer_id) override;
  void AcknowledgeFrameError() override;
  void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                           SetRegionOfInterestCallback callback) override;
  void SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) override;
  void GetImageFormats(GetImageFormatsCallback callback) override;

 private:
  // Closes the Stream connection, sending the given |status| to the client, and cleans up
  // outstanding state with the ISP.
  void Shutdown(zx_status_t status);

  bool started_ = false;
  fidl::Binding<fuchsia::camera2::Stream> binding_;
  fit::function<void(void)> disconnect_handler_;
  ProcessNode& output_node_;
  fxl::ThreadChecker thread_checker_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_
