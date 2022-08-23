// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/pipeline_manager.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <csignal>
#include <list>
#include <numeric>
#include <stack>
#include <type_traits>

#include "src/camera/drivers/controller/gdc_node.h"
#include "src/camera/drivers/controller/ge2d_node.h"
#include "src/camera/drivers/controller/input_node.h"
#include "src/camera/drivers/controller/output_node.h"
#include "src/camera/drivers/controller/passthrough_node.h"
#include "src/camera/drivers/controller/util.h"
#include "src/camera/lib/formatting/formatting.h"
#include "src/lib/digest/digest.h"

namespace camera {

PipelineManager::PipelineManager(async_dispatcher_t* dispatcher,
                                 const ddk::SysmemProtocolClient& sysmem,
                                 const ddk::IspProtocolClient& isp,
                                 const ddk::GdcProtocolClient& gdc,
                                 const ddk::Ge2dProtocolClient& ge2d,
                                 LoadFirmwareCallback load_firmware)
    : dispatcher_(dispatcher),
      isp_(isp),
      gdc_(gdc),
      ge2d_(ge2d),
      memory_allocator_(sysmem),
      load_firmware_(std::move(load_firmware)) {}

PipelineManager::~PipelineManager() {
  ZX_ASSERT_MSG(state_ == State::Uninitialized,
                "Caller destroying pipeline manager without awaiting shutdown completion.");
}

void PipelineManager::SetRoots(const std::vector<InternalConfigNode>& roots) {
  TRACE_DURATION("camera", "PipelineManager::SetRoots");
  ZX_ASSERT(state_ == State::Uninitialized);
  state_ = State::Configured;
  current_roots_ = roots;
}

// Helper to check whether a config node supports a given stream type.
static bool Supports(const InternalConfigNode& node, fuchsia::camera2::CameraStreamType type) {
  return std::any_of(node.supported_streams.cbegin(), node.supported_streams.cend(),
                     [type](const auto& supported) { return type == supported.type; });
}

void PipelineManager::ConfigureStreamPipeline(
    StreamCreationData info, fidl::InterfaceRequest<fuchsia::camera2::Stream> request) {
  TRACE_DURATION("camera", "PipelineManager::ConfigureStreamPipeline");
  ZX_ASSERT_MSG(state_ == State::Configured,
                "caller tried to create a new pipeline while the current one is still shutting "
                "down or roots are not set");
  // Queue this request for later execution if the pipeline is currently changing.
  if (pipeline_changing_) {
    pending_requests_.configure_stream_pipeline.emplace(std::move(info), std::move(request));
    return;
  }

  // The flag is set defensively for the duration of this method. Although it always completes prior
  // to returning control to the loop, this helps ensure that future changes to othjer parts of the
  // method body do not introduce callbacks that would lead to erroneous behavior.
  SetPipelineChanging(true);

  // Start with an empty path and the roots of the current configuration.
  FrameGraph::Node::Path path;
  auto candidates = std::cref(info.roots);

  // Advance down the configuration tree until an output node is found, creating intermediate nodes
  // along the way.
  while (!candidates.get().empty()) {
    std::vector<uint8_t> matches;
    ZX_ASSERT(candidates.get().size() < std::numeric_limits<uint8_t>::max());
    for (uint8_t i = 0; i < candidates.get().size(); ++i) {
      if (Supports(candidates.get()[i], info.stream_type())) {
        matches.push_back(i);
      }
    }
    if (matches.empty() || matches.size() > 1) {
      const auto reason = matches.empty() ? "no matches" : "multiple matches";
      FX_LOGS(FATAL) << "invalid product config - " << reason << " for stream type "
                     << formatting::ToString(info.stream_type()) << " at path " << Format(path);
    }
    path.push_back(matches[0]);
    auto& current = candidates.get()[path.back()];
    if (!graph_.Contains(path)) {
      // If there is no node associated with the current config node, create it.
      auto path_complete = CreateFGNode(info, path, request);
      if (path_complete) {
        SetPipelineChanging(false);
        return;
      }
    }
    candidates = current.child_nodes;
  }
  FX_LOGS(FATAL) << "invalid product config - no outputs below path " << Format(path);
}

void PipelineManager::SetStreamingEnabled(bool enabled) {
  streaming_enabled_ = enabled;
  UpdateInputNodeStreamingState();
}

void PipelineManager::Shutdown(fit::closure callback) {
  TRACE_DURATION("camera", "PipelineManager::Shutdown", "this", this);
  FX_LOGS(INFO) << "PipelineManager::Shutdown() - start";
  ZX_ASSERT_MSG(state_ != State::ShuttingDown, "Caller requested shutdown multiple times.");
  shutdown_state_.flow_nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("camera", "PipelineManager::ShutdownFlow", shutdown_state_.flow_nonce);
  // Set the shutdown flag and save the callback to be invoked upon completion of shutdown.
  state_ = State::ShuttingDown;
  shutdown_state_.callback = std::move(callback);
  // Discard any pending stream requests received before the shutdown, since they would just observe
  // peer-closed anyway. Requests received subsequent to the shutdown will still be handled after it
  // completes.
  pending_requests_.configure_stream_pipeline = {};
  // If the pipeline is changing, defer starting the shutdown.
  if (pipeline_changing_) {
    return;
  }
  // Otherwise, perform it immediately.
  SetPipelineChanging(true);
  ShutdownImpl();
}

void PipelineManager::ShutdownImpl() {
  TRACE_DURATION("camera", "PipelineManager::ShutdownImpl", "this", this);
  // Locate all active outputs in the graph. These are copied to a separate vector before removing
  // them as doing so invalidates the map iterator.
  shutdown_state_.started = true;
  std::vector<std::vector<uint8_t>> output_paths;
  for (auto& [key, node] : graph_.nodes) {
    if (node.children.empty()) {
      output_paths.push_back(key);
    }
  }
  if (output_paths.empty()) {
    SetPipelineChanging(false);
    return;
  }
  for (auto& path : output_paths) {
    ShutdownAndRemoveNode(path);
  }
}

// TODO(fxbug.dev/100525): the notion of "shutdown" being a required step
void PipelineManager::SetPipelineChanging(bool changing) {
  TRACE_DURATION("camera", "PipelineManager::SetPipelineChanging", "changing", changing);
  ZX_ASSERT_MSG(changing != pipeline_changing_, "pipeline already %s",
                changing ? "changing" : "stable");
  if (changing) {
    pipeline_changing_ = true;
    return;
  }
  // When transitioning from changing to stable, schedule a task to handle all accumulated requests.
  // A task is used (vs. invoking the handlers immediately) in order to avoid unconstrained
  // recursion. The requests are moved out of the main queue first in order to avoid an endless loop
  // if the request relies on any async steps in order to complete.
  async::PostTask(dispatcher_, [this] {
    pipeline_changing_ = false;
    // First handle any pending disconnects.
    auto disconnects = std::move(pending_requests_.disconnects);
    pending_requests_.disconnects = {};
    while (!disconnects.empty()) {
      ClientDisconnect(disconnects.front());
      disconnects.pop();
    }
    // If any of the previous disconnects triggered a pipeline change, skip further handling as they
    // will be handled upon subsequent transition to stable.
    if (pipeline_changing_) {
      return;
    }
    // Handle shutdown if it has been called. There should be no pending requests in this state.
    if (state_ == State::ShuttingDown) {
      ZX_ASSERT(pending_requests_.configure_stream_pipeline.empty());
      if (shutdown_state_.started) {
        // If the pipeline was actively shutting down, a transition to stable indicates this process
        // has completed.
        TRACE_FLOW_END("camera", "PipelineManager::ShutdownFlow", shutdown_state_.flow_nonce);
        auto callback = std::move(shutdown_state_.callback);
        shutdown_state_.callback = nullptr;
        shutdown_state_.started = false;
        state_ = State::Uninitialized;
        callback();
      } else {
        // Otherwise, begin the shutdown process.
        pipeline_changing_ = true;
        ShutdownImpl();
      }
      return;
    }
    // Handle any pending connection requests.
    auto requests = std::move(pending_requests_.configure_stream_pipeline);
    pending_requests_.configure_stream_pipeline = {};
    while (!requests.empty()) {
      std::apply(fit::bind_member(this, &PipelineManager::ConfigureStreamPipeline),
                 std::move(requests.front()));
      requests.pop();
    }
  });
}

void PipelineManager::UpdateInputNodeStreamingState() {
  for (uint32_t i = 0; i < current_roots_.size(); ++i) {
    auto input = graph_.nodes.find({static_cast<uint8_t>(i)});
    if (input != graph_.nodes.end()) {
      auto node = static_cast<InputNode*>(input->second.process_node.get());
      // The input node is robust to idempotent start/stop requests.
      FX_LOGS(INFO) << this << ": camera pipeline streaming "
                    << (streaming_enabled_ ? "enabled" : "disabled");
      if (streaming_enabled_) {
        node->StartStreaming();
      } else {
        node->StopStreaming();
      }
    }
  }
}

void PipelineManager::ShutdownAndRemoveNode(const std::vector<uint8_t>& path) {
  TRACE_DURATION("camera", "PipelineManager::ShutdownAndRemoveNode", "path", Format(path));
  ZX_ASSERT(pipeline_changing_);
  graph_.nodes.at(path).process_node->Shutdown([this, path = path] {
    auto target = graph_.nodes.extract(path);
    auto parent = path;
    parent.pop_back();
    if (!parent.empty()) {
      FX_LOGS(INFO) << "Removing " << Format(path) << " from " << Format(parent) << " child set";
      graph_.nodes.at(parent).children.erase(path);
    }
    Prune();
  });
}

void PipelineManager::Prune() {
  TRACE_DURATION("camera", "PipelineManager::Prune");
  ZX_ASSERT(pipeline_changing_);
  for (auto& [key, value] : graph_.nodes) {
    if (value.children.empty() && value.process_node->Type() != kOutputStream) {
      async::PostTask(dispatcher_, [this, path = key] { ShutdownAndRemoveNode(path); });
      return;
    }
  }
  FX_LOGS(INFO) << "Prune() - no prune targets found";
  SetPipelineChanging(false);
}

std::vector<fuchsia::sysmem::BufferCollectionConstraints> PipelineManager::GatherOutputConstraints(
    const camera::InternalConfigNode& node) {
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  if (node.output_constraints) {
    constraints.push_back(*node.output_constraints);
  }
  for (const auto& child : node.child_nodes) {
    if (child.input_constraints) {
      constraints.push_back(*child.input_constraints);
    }
    if (!child.output_constraints) {
      constraints += GatherOutputConstraints(child);
    }
  }
  return constraints;
}

std::string PipelineManager::Dump(bool log, cpp20::source_location location) const {
  std::stringstream ss;
  ss << "graph_:\n";
  for (auto& [key, node] : graph_.nodes) {
    ss << "  " << Format(key) << ":\n";
    ss << "    path: " << Format(key) << "\n";
    ss << "    buffers: ";
    if (node.output_buffers) {
      auto& buffers = node.output_buffers->buffers;
      ss << "[" << buffers.buffer_count << "] "
         << (buffers.settings.buffer_settings.size_bytes / 1024) << " KiB ("
         << buffers.settings.image_format_constraints.min_coded_width << "x"
         << buffers.settings.image_format_constraints.min_coded_height << ")\n";
    } else {
      ss << "<none>\n";
    }
    ss << "    children: \n";
    for (auto& child : node.children) {
      ss << "      " << Format(child) << "\n";
    }
  }
  auto str = ss.str();
  if (log) {
    // Derivation of FX_LOGS(severity) that accepts custom FILE and LINE parameters.
    FX_LAZY_STREAM(::syslog::LogMessage(::syslog::LOG_INFO, location.file_name(), location.line(),
                                        nullptr, nullptr)
                       .stream(),
                   FX_LOG_IS_ON(INFO))
        << "PipelineManager::Dump()\n"
        << str;
  }
  return str;
}

// Returns a list of nodes corresponding to the given path.
// TODO(100525): Ideally the controller would just create a bidirectionally traversible
// representation of the product config when first loaded.
std::list<std::reference_wrapper<const InternalConfigNode>> GetPathICNodes(
    const std::vector<InternalConfigNode>& roots, std::vector<uint8_t> path) {
  if (path.empty()) {
    return {};
  }
  std::list<std::reference_wrapper<const InternalConfigNode>> nodes{roots[path[0]]};
  for (size_t i = 1; i < path.size(); ++i) {
    nodes.push_back(nodes.back().get().child_nodes[path[i]]);
  }
  return nodes;
}

bool PipelineManager::CreateFGNode(const StreamCreationData& info, const std::vector<uint8_t>& path,
                                   fidl::InterfaceRequest<fuchsia::camera2::Stream>& request) {
#ifndef NDEBUG
  Dump();
#endif
  ZX_ASSERT(!graph_.Contains(path));
  auto path_icnodes = GetPathICNodes(info.roots, path);
  auto icself = path_icnodes.back().get();
  FrameGraph::Node fgself{.pulldown{.interval = FramerateToInterval(icself.output_frame_rate)}};

  ProcessNode::BufferAttachments attachments{};
  if (icself.input_constraints) {
    // Most nodes have input constraints (meaning it receives data from a preceding node). Search
    // backwards along the current path for the closest producer node. This may not be the immediate
    // parent, e.g. if the parent has no direct outputs and just operates "in place" on an input
    // collection.
    auto ancestor_path = path;
    auto ancestor_icnodes = path_icnodes;
    do {
      ancestor_path.pop_back();
      ancestor_icnodes.pop_back();
    } while (!ancestor_path.empty() && !graph_.nodes.at(ancestor_path).output_buffers);
    ZX_ASSERT(!ancestor_path.empty());
    auto& ancestor_fgnode = graph_.nodes.at(ancestor_path);
    attachments.input_collection = *ancestor_fgnode.output_buffers;
    attachments.input_formats = ancestor_icnodes.back().get().image_formats;
  }

  if (icself.output_constraints) {
    // Allocate a buffer collection by aggregating the current node's output constraints and all
    // downstream input constraints. Downstream constraints propagate through "in place" processing
    // nodes.
    auto constraints = GatherOutputConstraints(icself);
    BufferCollection buffers{};
    zx_status_t status =
        memory_allocator_.AllocateSharedMemory(constraints, buffers, NodeTypeName(icself));
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "unable to allocate memory";
    }
    fgself.output_buffers = std::move(buffers);
    attachments.output_collection = *fgself.output_buffers;
    attachments.output_formats = icself.image_formats;
  } else {
    // A node that has no output constraints operates "in place" on the input collection it is
    // provided, either modifying buffer content (GE2D) or sending it externally (output node). To
    // avoid requiring special handling for these cases, the outputs are set to point to the same
    // attachments as the inputs.
    attachments.output_collection = attachments.input_collection;
    attachments.output_formats = attachments.input_formats;
  }

  // The inter-node frame handler distributes an inbound frame to all active children.
  auto frame_handler = [this, path](const ProcessNode::FrameToken& token,
                                    frame_metadata_t metadata) {
    std::stringstream ss;
    auto& self_fnode = graph_.nodes.at(path);
    for (auto& child_path : self_fnode.children) {
      auto& child_fnode = graph_.nodes.at(child_path);
      if (child_fnode.pulldown.accumulator <= numerics::Rational{0}) {
        child_fnode.pulldown.accumulator += child_fnode.pulldown.interval;
        ZX_ASSERT(child_fnode.process_node);
        child_fnode.process_node->ProcessFrame(token, metadata);
      }
      child_fnode.pulldown.accumulator -= self_fnode.pulldown.interval;
    }
  };

  switch (icself.type) {
    // Input
    case NodeType::kInputStream: {
      auto result =
          InputNode::Create(dispatcher_, attachments, std::move(frame_handler), isp_, info);
      ZX_ASSERT(result.is_ok());
      fgself.process_node = result.take_value();
    } break;
    // GDC
    case NodeType::kGdc: {
      auto result = GdcNode::Create(dispatcher_, attachments, std::move(frame_handler),
                                    load_firmware_, gdc_, icself, info);
      ZX_ASSERT(result.is_ok());
      fgself.process_node = result.take_value();
    } break;
    // GE2D
    case NodeType::kGe2d: {
      auto result = camera::Ge2dNode::Create(dispatcher_, attachments, std::move(frame_handler),
                                             load_firmware_, ge2d_, icself, info);
      ZX_ASSERT(result.is_ok());
      fgself.process_node = result.take_value();
    } break;
    // Output
    case NodeType::kOutputStream: {
      auto result = camera::OutputNode::Create(
          dispatcher_, attachments, std::move(request),
          {.disconnect = [this, path] { ClientDisconnect(path); },
           .set_region_of_interest =
               [this, path](auto x_min, auto y_min, auto x_max, auto y_max, auto callback) {
                 SetRegionOfInterest(path, x_min, y_min, x_max, y_max, std::move(callback));
               },
           .set_image_format =
               [this, path](auto image_format_index, auto callback) {
                 SetImageFormat(path, image_format_index, std::move(callback));
               },
           .get_image_formats =
               [this, path](auto callback) { GetImageFormats(path, std::move(callback)); },
           .get_buffers = [this, path](auto callback) { GetBuffers(path, std::move(callback)); }});
      ZX_ASSERT(result.is_ok());
      fgself.process_node = result.take_value();
    } break;
    // Passthrough
    case NodeType::kPassthrough: {
      auto result = camera::PassthroughNode::Create(dispatcher_, attachments,
                                                    std::move(frame_handler), icself, info);
      ZX_ASSERT(result.is_ok());
      fgself.process_node = result.take_value();
    } break;
    default:
      ZX_PANIC("unsupported stream type %d", icself.type);
  }
  fgself.process_node->SetLabel(Format(path) + "(" + NodeTypeName(icself) + ")");

  // Once the node has been created successfully, attach it to its relatives and add it to the
  // graph.
  if (path.size() > 1) {
    auto parent_path = FrameGraph::Node::Path{path.begin(), path.end() - 1};
    auto& fgparent = graph_.nodes.at(parent_path);
    fgparent.children.insert(path);
  }
  graph_.nodes.emplace(path, std::move(fgself));
#ifndef NDEBUG
  Dump();
#endif

  // Return true if the configured node was an output. This indicates that the `request` channel was
  // consumed and the pipeline is complete.
  return icself.type == kOutputStream;
}

void PipelineManager::ClientDisconnect(const std::vector<uint8_t>& origin) {
  TRACE_DURATION("camera", "PipelineManager::ClientDisconnect", "origin", &origin);
  FX_LOGS(INFO) << "ClientDisconnect(" << Format(origin) << ")";
  if (pipeline_changing_) {
    pending_requests_.disconnects.push(origin);
    return;
  }
  SetPipelineChanging(true);
  ShutdownAndRemoveNode(origin);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): protocol defined elsewhere
void PipelineManager::SetRegionOfInterest(
    const std::vector<uint8_t>& origin, float x_min, float y_min, float x_max, float y_max,
    fuchsia::camera2::Stream::SetRegionOfInterestCallback callback) {
  auto local_path = origin;
  auto path_icnodes = GetPathICNodes(current_roots_, local_path);
  while (!local_path.empty()) {
    auto& icnode = path_icnodes.back().get();
    auto& fgnode = graph_.nodes.at(local_path);
    if (icnode.type == kGe2d && icnode.ge2d_info.config_type == GE2D_RESIZE) {
      BindPolyMethod(fgnode.process_node.get(), &Ge2dNode::SetCropRect);
      static_cast<Ge2dNode*>(fgnode.process_node.get())->SetCropRect(x_min, y_min, x_max, y_max);
    }
    local_path.pop_back();
    path_icnodes.pop_back();
  }
  callback(ZX_OK);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

void PipelineManager::SetImageFormat(const std::vector<uint8_t>& origin,
                                     uint32_t image_format_index,
                                     fuchsia::camera2::Stream::SetImageFormatCallback callback) {
  // The existing implementation assumes that all nodes in the pipeline have either one image
  // format or support the same number of formats. When a new format is requested, all nodes in
  // the graph are notified. For most, the request does nothing.
  auto local_path = origin;
  auto path_icnodes = GetPathICNodes(current_roots_, local_path);
  if (image_format_index >= path_icnodes.back().get().image_formats.size()) {
    callback(ZX_ERR_INVALID_ARGS);
    return;
  }
  while (!local_path.empty()) {
    auto& icnode = path_icnodes.back().get();
    auto& fgnode = graph_.nodes.at(local_path);
    if (icnode.image_formats.size() > 1) {
      fgnode.process_node->SetOutputFormat(image_format_index, [] {});
    }
    local_path.pop_back();
    path_icnodes.pop_back();
  }
  callback(ZX_OK);
}

void PipelineManager::GetImageFormats(const std::vector<uint8_t>& origin,
                                      fuchsia::camera2::Stream::GetImageFormatsCallback callback) {
  auto path_icnodes = GetPathICNodes(current_roots_, origin);
  auto& icnode = path_icnodes.back().get();
  callback({icnode.image_formats.begin(), icnode.image_formats.end()});
}

void PipelineManager::GetBuffers(const std::vector<uint8_t>& origin,
                                 fuchsia::camera2::Stream::GetBuffersCallback callback) {
  // in_place nodes may not have bound buffer collection ptr, walk up to find the real collection.
  auto local_path = origin;
  local_path.pop_back();
  while (!local_path.empty() && !graph_.nodes.at(local_path).output_buffers) {
    local_path.pop_back();
  }
  if (local_path.empty()) {
    FX_LOGS(FATAL) << "no output buffers found in ancestors of " << Format(origin);
  }
  auto& fgnode = graph_.nodes.at(local_path);
  auto& input_buffer_collection = fgnode.output_buffers->ptr;
  ZX_ASSERT(input_buffer_collection);
  fuchsia::sysmem::BufferCollectionTokenHandle token;
  input_buffer_collection->AttachToken(ZX_RIGHT_SAME_RIGHTS, token.NewRequest());
  input_buffer_collection->Sync(
      [callback = std::move(callback), token = std::move(token)]() mutable {
        callback(std::move(token));
      });
}

bool PipelineManager::FrameGraph::Contains(const Node::Path& path) const {
  return nodes.find(path) != nodes.end();
}

}  // namespace camera
