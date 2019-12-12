// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/compiler.h>

#include <queue>
#include <unordered_set>

#include <ddktl/protocol/isp.h>
#include <fbl/mutex.h>

namespace camera {

class ProcessNode;
// Server-side implementation of a stream.
class StreamImpl : public fuchsia::camera2::Stream {
 public:
  explicit StreamImpl(async_dispatcher_t* dispatcher, ProcessNode* output_node);

  // Binds a channel to the stream.
  // Args:
  //   |channel|: the channel to bind
  //   |disconnect_handler|: called when the client on |channel| disconnects
  zx_status_t Attach(zx::channel channel, fit::function<void(void)> disconnect_handler);

  void FrameReady(uint32_t buffer_index);

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

  async_dispatcher_t* dispatcher_;
  bool started_ = false;
  fidl::Binding<fuchsia::camera2::Stream> binding_;
  fit::function<void(void)> disconnect_handler_;
  fbl::Mutex event_queue_lock_;
  ProcessNode& output_node_;
  std::queue<async::TaskClosure> event_queue_ __TA_GUARDED(event_queue_lock_);
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PROTOCOL_H_
