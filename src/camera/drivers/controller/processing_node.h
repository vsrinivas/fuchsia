// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/thread_checker.h>
#include <lib/stdcompat/source_location.h>
#include <zircon/assert.h>

#include <mutex>
#include <queue>
#include <vector>

#include <fbl/macros.h>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/lib/tokens/tokens.h"

namespace camera {

// A ProcessNode is an abstract base class that represents a logical or physical functional unit
// within the camera hardware pipeline. A node owns its output collection but not its input
// collection. Derived classes override the various methods based on their need.
class ProcessNode {
 public:
  // Wrapper for a set of input/output buffer collections and their associated formats.
  struct BufferAttachments {
    std::optional<std::reference_wrapper<BufferCollection>> input_collection;
    std::optional<std::reference_wrapper<const std::vector<fuchsia::sysmem::ImageFormat_2>>>
        input_formats;
    std::optional<std::reference_wrapper<BufferCollection>> output_collection;
    std::optional<std::reference_wrapper<const std::vector<fuchsia::sysmem::ImageFormat_2>>>
        output_formats;
  };

  using FrameToken = SharedToken<const uint32_t>;

  // FrameCallback is a caller-provided handler that nodes should invoke when they have produced a
  // new frame.
  using FrameCallback = fit::function<void(FrameToken, frame_metadata_t)>;
  ProcessNode(async_dispatcher_t* dispatcher, NodeType type, BufferAttachments attachments,
              FrameCallback frame_callback);

  // Destroys the node instance. The caller must call Shutdown before destroying the instance or
  // the process will terminate. If the node implementation relies on singleton hardware resources
  // that cannot be safely accessed concurrently by multiple instances of the class, then the node
  // must ensure that all such resources are released prior returning from this destructor.
  virtual ~ProcessNode();

  // Returns the NodeType specified on creation of the object.
  NodeType Type() const;

  // Informs the node that it should begin processing the frame specified by the given token and
  // associated with the given metadata. The node must maintain the token until it is no longer
  // needed, after which point it can safely be destroyed.
  virtual void ProcessFrame(FrameToken token, frame_metadata_t metadata) = 0;

  // Requests that the node node begin producing frames using the specified format index. As the
  // node may pipeline frames, it is acceptable to continue producing frames at the previous
  // format. But the node must invoke the provided callback after the last frame at the previous
  // format was sent and before the first frame of the new format is sent.
  //
  // The caller may request multiple format changes without receiving frames. In these cases,
  // the node must invoke all callbacks in the order received, but it does not need to produce a
  // frame with each format requested. For example, if the caller requests a change from A to B to
  // C, it is okay to produce a frame with format A, then invoke callback B followed by C, and then
  // produce a frame with format C.
  virtual void SetOutputFormat(uint32_t output_format_index, fit::closure callback) = 0;

  // Requests that the node cease processing frames and begin shutting itself down. The node must
  // perform any flushing required in order to safely return frames that it received via
  // ProcessFrame calls. The node must ensure all pending ProcessFrame callbacks are appropriately
  // invoked, and then invoke the callback specified in this method, after which the node must make
  // no further calls to the frame_callback provided during creation. After requesting shutdown, the
  // caller will ensure that no further calls are made to the node.
  void Shutdown(fit::closure callback);

  // Assigns a label to the node for logging purposes.
  void SetLabel(std::string label);

 protected:
  // Nodes should use this method to invoke the top-level FrameCallback this node was created with.
  void SendFrame(uint32_t index, frame_metadata_t metadata, fit::closure release_callback) const;

  // Provides the node access to the input buffer collection it was created with.
  const fuchsia::sysmem::BufferCollectionInfo_2& InputBuffers() const;

  // Provides the node access to the image formats associated with its inputs.
  const std::vector<fuchsia::sysmem::ImageFormat_2>& InputFormats() const;

  // Provides the node access to the output buffer collection it was created with.
  const fuchsia::sysmem::BufferCollectionInfo_2& OutputBuffers() const;

  // Provides the node access to the image formats associated with its outputs.
  const std::vector<fuchsia::sysmem::ImageFormat_2>& OutputFormats() const;

  // fuchsia.hardware.camerahwaccel.*Callback implementations. The node receives these callbacks
  // serially on the dispatcher it was created with.
  virtual void HwFrameReady(frame_available_info_t info) = 0;
  virtual void HwFrameResolutionChanged(frame_available_info_t info) = 0;
  virtual void HwTaskRemoved(task_remove_status_t status) = 0;

  // Nodes must implement this method. It is distinct from the non-virtual Shutdown method in order
  // to allow the base class visibility into the state of the node's shutdown. Refer to the
  // description of that method for details.
  virtual void ShutdownImpl(fit::closure callback) = 0;

  // Returns c-style callback pointers. When invoked by a consumer, the corresponding Hw* callback
  // is invoked via the node's dispatcher. The callsite location is used to annotate the callback
  // logs and trace events.
  const hw_accel_frame_callback* GetHwFrameReadyCallback(
      cpp20::source_location location = cpp20::source_location::current());
  const hw_accel_res_change_callback* GetHwFrameResolutionChangeCallback(
      cpp20::source_location location = cpp20::source_location::current());
  const hw_accel_remove_task_callback* GetHwTaskRemovedCallback(
      cpp20::source_location location = cpp20::source_location::current());

  // Convenience method that wraps a call to async::PostTask with trace markers. These produce flow
  // graph indicators which allow tracing the origin of a callback. By default, the trace events are
  // annotated with the callsite of this method.
  void PostTask(fit::closure task,
                cpp20::source_location location = cpp20::source_location::current());

 private:
  // Static methods bound to the c-style callbacks.
  static void StaticHwFrameReady(void* ctx, const frame_available_info_t* info);
  static void StaticHwFrameResolutionChanged(void* ctx, const frame_available_info_t* info);
  static void StaticHwTaskRemoved(void* ctx, task_remove_status_t status);

  // Dispatcher for the frame processing loop.
  async_dispatcher_t* dispatcher_;
  // Indicates the specific sub-type of process node.
  const NodeType type_;
  // Buffer collections and formats attached to the node.
  BufferAttachments attachments_;
  // Caller-provided callback to be invoked by the node when a new frame is available.
  FrameCallback frame_callback_;
  // Containers for the c-style callbacks. These are associated with the OnHw* callbacks.
  struct {
    const hw_accel_frame_callback frame;
    std::vector<cpp20::source_location> frame_callsites;
    const hw_accel_res_change_callback res_change;
    std::vector<cpp20::source_location> res_change_callsites;
    const hw_accel_remove_task_callback remove_task;
    std::vector<cpp20::source_location> remove_task_callsites;
  } hwaccel_callbacks_;
  // Shutdown state tracking for caller validation.
  struct {
    bool requested = false;
    bool completed = false;
  } shutdown_state_;
  std::string label_ = "<unset>";
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
