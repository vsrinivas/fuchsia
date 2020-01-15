// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <zircon/assert.h>

#include "processing_node.h"
#include "stream_pipeline_info.h"
#include "stream_protocol.h"

// |OutputNode| represents a |ProcessNode| of NodeType |kOutputStream|.
// This node handles the communication with the clients.
namespace camera {
class OutputNode : public ProcessNode {
 public:
  OutputNode(async_dispatcher_t* dispatcher, ProcessNode* parent_node,
             fuchsia::camera2::CameraStreamType current_stream_type,
             std::vector<fuchsia::camera2::CameraStreamType> supported_streams,
             fuchsia::camera2::FrameRate frame_rate)
      : ProcessNode(NodeType::kOutputStream, parent_node, current_stream_type,
                    std::move(supported_streams), dispatcher, frame_rate){};

  // Creates an |OutputNode| object.
  // Args:
  // |dispatcher| : Dispatcher on which GDC tasks can be queued up.
  // |info| : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  static fit::result<OutputNode*, zx_status_t> CreateOutputNode(
      async_dispatcher_t* dispatcher, StreamCreationData* info, ProcessNode* parent_node,
      const InternalConfigNode& internal_output_node);

  // Binds a channel to the stream.
  // Args:
  //   |channel|: the channel to bind
  //   |disconnect_handler|: called when the client on |channel| disconnects.
  zx_status_t Attach(zx::channel channel, fit::function<void(void)> disconnect_handler);

  void set_client_stream(std::unique_ptr<StreamImpl> client_stream) {
    client_stream_ = std::move(client_stream);
  }
  std::unique_ptr<camera::StreamImpl>& client_stream() { return client_stream_; }

  // Notifies that a frame is ready to be sent to the client.
  void OnReadyToProcess(const frame_available_info_t* info) override;

  // Called by the client to release a frame.
  void OnReleaseFrame(uint32_t buffer_index) override;

  // Shut down routine.
  void OnShutdown(fit::function<void(void)> shutdown_callback) override {
    shutdown_requested_ = true;
    shutdown_callback();
  }

 private:
  std::unique_ptr<StreamImpl> client_stream_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_
