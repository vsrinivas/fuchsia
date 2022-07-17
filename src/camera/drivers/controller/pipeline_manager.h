// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>

#include <map>
#include <ratio>
#include <set>
#include <vector>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/drivers/controller/util.h"
#include "src/camera/lib/numerics/rational.h"

namespace camera {

// |PipelineManager|
// This class provides a way to create the stream pipeline for a particular
// stream configuration requested.
// While doing so it would also create ISP stream protocol and client stream protocols
// and setup the camera pipeline such that the streams are flowing properly as per the
// requested stream configuration.
// Each "ProcessNode" represents a logical or physical hardware unit. A graph begins at an "input"
// i.e. the output of the ISP (there are two of these on Sherlock) and eventually connects to the
// server end of a fuchsia::camera2::Stream channel. A node can have multiple downstream nodes but
// only one upstream node. In other words, all nodes have a fan-in of 1, except the input node which
// has a fan-in of 0 (it generates its own frames unsolicited).
// The supported graph layouts are defined by the ProductConfig used by the caller. Individual
// elements from it are passed in to methods on the PipelineManager. The connections themselves are
// not explicit, but are determined by the "supported stream types" of a given node. These uniquely
// define the required connectivity between nodes in the graph.
// A "config node" is an element within the product config data structure, which statically defines
// the supported connectivity to other config nodes. ProcessNodes are created using parameters from
// a specific ConfigNode.
// TODO(100525): Rationalize the different "node" names e.g. fnode, inode, pnode = frame graph,
// internal config, and process, respectively.
class PipelineManager {
 public:
  PipelineManager(async_dispatcher_t* dispatcher, const ddk::SysmemProtocolClient& sysmem,
                  const ddk::IspProtocolClient& isp, const ddk::GdcProtocolClient& gdc,
                  const ddk::Ge2dProtocolClient& ge2d, LoadFirmwareCallback load_firmware);
  ~PipelineManager();

  // Sets the roots (inputs) of the pipeline. The pipeline must have no active streams.
  void SetRoots(const std::vector<InternalConfigNode>& roots);

  // Attaches the provided stream request to the pipeline using the provided info structure,
  // creating intermediate nodes as necessary.
  void ConfigureStreamPipeline(StreamCreationData info,
                               fidl::InterfaceRequest<fuchsia::camera2::Stream> request);

  // Suppresses sending frames to all output clients.
  void SetStreamingEnabled(bool enabled);

  // Requests shutdown of the current pipeline. Callers must call this method and await the callback
  // before destroying the class or calling SetRoots or ConfigureStreamPipeline with a new
  // configuration index.
  void Shutdown(fit::closure callback);

 private:
  // Performs the actual shutdown.
  void ShutdownImpl();

  // Marks the pipeline as changing or stable. Asserts that the state is not currently the same as
  // is specified. After transitioning from changing to stable, any pending requests are handled in
  // the order they were received in.
  // TODO(100525): mutex annotations could enforce some of the required semantics at compile time
  void SetPipelineChanging(bool changing);

  // Updates the streaming state of all active roots in the current tree.
  void UpdateInputNodeStreamingState();

  // This method returns the set of constraints that apply to the specified node's output
  // collection. It reflects the node's own output constraints, and all downstream input
  // constraints, including those further downstream of "in-place" children.
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> GatherOutputConstraints(
      const camera::InternalConfigNode& node);

  // Produces a text representation of the current state of the pipeline, optionally adding it to
  // syslog as an ERROR message associated with the method callsite.
  std::string Dump(bool log = true,
                   cpp20::source_location location = cpp20::source_location::current()) const;

  // Creates a new node and appends it to the frame graph.
  // Args:
  // |parent| : Internal node of the node where this new node needs to be created.
  // |self| : Internal node describing the new node to be created.
  // |request| : The request to attach to the output node for the requested stream type. If |self|
  // is an output node, |request| is consumed. Otherwise, it is unmodified.
  bool CreateFGNode(const StreamCreationData& info, const std::vector<uint8_t>& path,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream>& request);

  // Shuts down and removes the specified node from the frame graph, then invokes Prune.
  void ShutdownAndRemoveNode(const std::vector<uint8_t>& path);

  // Asserts that the changing flag is set, then searches the frame graph for non-output leaf nodes
  // with no children. If one is found, it is shut down and removed, and Prune is run again. If no
  // prunable nodes are found, the pipeline is marked stable.
  void Prune();

  // Stream callback handlers.
  void ClientDisconnect(const std::vector<uint8_t>& origin);
  void SetRegionOfInterest(const std::vector<uint8_t>& origin, float x_min, float y_min,
                           float x_max, float y_max,
                           fuchsia::camera2::Stream::SetRegionOfInterestCallback callback);
  void SetImageFormat(const std::vector<uint8_t>& origin, uint32_t image_format_index,
                      fuchsia::camera2::Stream::SetImageFormatCallback callback);
  void GetImageFormats(const std::vector<uint8_t>& origin,
                       fuchsia::camera2::Stream::GetImageFormatsCallback callback);
  void GetBuffers(const std::vector<uint8_t>& origin,
                  fuchsia::camera2::Stream::GetBuffersCallback callback);

  async_dispatcher_t* dispatcher_;
  const ddk::IspProtocolClient& isp_;
  const ddk::GdcProtocolClient& gdc_;
  const ddk::Ge2dProtocolClient& ge2d_;
  ControllerMemoryAllocator memory_allocator_;
  // Caller-provided callback to load firmware from a given path into a VMO.
  LoadFirmwareCallback load_firmware_;

  // This object is used to queue public methods calls. If a call is received while the pipeline is
  // in the process of being reconfigured, the request is queued. Upon completion of the
  // reconfiguration process, these calls are handled in the order received.
  struct {
    std::queue<std::vector<uint8_t>> disconnects;
    using ConfigureStreamPipelineParams =
        std::tuple<StreamCreationData, fidl::InterfaceRequest<fuchsia::camera2::Stream>>;
    std::queue<ConfigureStreamPipelineParams> configure_stream_pipeline;
  } pending_requests_;
  bool pipeline_changing_ = false;
  bool streaming_enabled_ = true;

  std::vector<InternalConfigNode> current_roots_;

  // TODO(100525): this should probably be its own class so `nodes` can be private.
  // The FrameGraph represents the frame processing graph managed by the PipelineManager and serves
  // as a container for its elements.
  struct FrameGraph {
    FrameGraph() = default;
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;
    FrameGraph& operator=(FrameGraph&&) = delete;
    // A Node represents a ProcessNode and its relationships within the graph. ProcessNodes are not
    // aware of other ProcessNodes. Nodes can have multiple children but only one parent.
    struct Node {
      // The Path to a node is the sequence of indices from the root of the ProductConfig that leads
      // to a particular InternalConfigNode. This uniquely identifies the node within the config
      // tree, and has useful properties such as Path[0] always pointing to the root,
      // Path.pop_back() resulting in the path of the parent node, etc.
      using Path = std::vector<uint8_t>;
      // Stores the ProcessNode instance.
      std::unique_ptr<ProcessNode> process_node;
      // Nodes own their output buffer collection.
      std::optional<BufferCollection> output_buffers;
      // The node's active children. "Output" nodes do not have children.
      std::set<Path> children;
      // Framerate changes are accomplished by dropping a certain proportion of frames. This
      // container reflects the current state of that process.
      struct {
        // Target frame interval in seconds (reciprocal of framerate).
        const numerics::Rational interval;
        // Nominal time until the next frame is sent. When this value reaches zero, the next
        // available frame is sent onwards and the value is increased by `period`.
        numerics::Rational accumulator;
      } pulldown;
    };
    // The container for all nodes in the graph.
    std::map<Node::Path, Node> nodes;
    // Convenience method to check whether the `nodes` map contains the key `path`.
    bool Contains(const Node::Path& path) const;
  } graph_;

  // Shutdown state tracking for caller validation.
  enum class State { Uninitialized, Configured, ShuttingDown } state_ = State::Uninitialized;
  struct {
    uint64_t flow_nonce = 0;
    bool started = false;
    fit::closure callback;
  } shutdown_state_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
