// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <lib/fidl/cpp/binding.h>

#include <map>
#include <utility>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/drivers/controller/util.h"

// |OutputNode| represents a |ProcessNode| of NodeType |kOutputStream|.
// This node handles the communication with the clients.
namespace camera {

class OutputNode : public ProcessNode, public fuchsia::camera2::Stream {
 public:
  OutputNode(async_dispatcher_t* dispatcher, BufferAttachments attachments);

  // Container for caller-provided callbacks. The node invokes these when the Stream client
  // disconnects or calls the named method.
  struct Callbacks {
    // Invoked when the remote endpoint of the request channel is closed.
    fit::closure disconnect;
    // Invoked when the client calls fuchsia::camera2::Stream.SetRegionOfInterest
    fit::function<void(float, float, float, float, SetRegionOfInterestCallback)>
        set_region_of_interest;
    // Invoked when the client calls fuchsia::camera2::Stream.SetImageFormat
    fit::function<void(uint32_t, SetImageFormatCallback)> set_image_format;
    // Invoked when the client calls fuchsia::camera2::Stream.GetImageFormats
    fit::function<void(GetImageFormatsCallback)> get_image_formats;
    // Invoked when the client calls fuchsia::camera2::Stream.GetBuffers
    fit::function<void(GetBuffersCallback)> get_buffers;
  };

  // Creates an |OutputNode| object.
  // Args:
  // |dispatcher| : Dispatcher on which GDC tasks can be queued up.
  // |info| : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  //
  static fpromise::result<std::unique_ptr<OutputNode>, zx_status_t> Create(
      async_dispatcher_t* dispatcher, BufferAttachments attachments,
      fidl::InterfaceRequest<fuchsia::camera2::Stream> request, Callbacks callbacks);

  // | fuchsia::camera2::Stream |
  void Start() override;
  void Stop() override;
  void ReleaseFrame(uint32_t buffer_id) override;
  void AcknowledgeFrameError() override;
  void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                           SetRegionOfInterestCallback callback) override;
  void SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) override;
  void GetImageFormats(GetImageFormatsCallback callback) override;
  void GetBuffers(GetBuffersCallback callback) override;

 private:
  // ProcessNode method implementations.
  void ProcessFrame(FrameToken token, frame_metadata_t metadata) override;
  void SetOutputFormat(uint32_t output_format_index, fit::closure callback) override;
  void ShutdownImpl(fit::closure callback) override;

  // fuchsia.hardware.camerahwaccel.*Callback implementations.
  void HwFrameReady(frame_available_info_t info) override;
  void HwFrameResolutionChanged(frame_available_info_t info) override;
  void HwTaskRemoved(task_remove_status_t status) override;

  async_dispatcher_t* dispatcher_;
  Callbacks callbacks_;
  bool started_ = false;
  fidl::Binding<fuchsia::camera2::Stream> binding_;
  std::map<uint32_t, FrameToken> client_tokens_;
  fuchsia::sysmem::BufferCollectionPtr observer_collection_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_OUTPUT_NODE_H_
