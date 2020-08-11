// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pipeline_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <ddk/trace/event.h>
#include <fbl/auto_call.h>

#include "graph_utils.h"

namespace camera {

constexpr auto kTag = "camera_controller";

fit::result<OutputNode*, zx_status_t> PipelineManager::CreateGraph(
    StreamCreationData* info, const InternalConfigNode& internal_node, ProcessNode* parent_node) {
  fit::result<OutputNode*, zx_status_t> result;
  const auto* next_node_internal = GetNextNodeInPipeline(info->stream_type(), internal_node);
  if (!next_node_internal) {
    FX_LOGST(ERROR, kTag) << "Failed to get next node";
    return fit::error(ZX_ERR_INTERNAL);
  }

  switch (next_node_internal->type) {
    // Input Node
    case NodeType::kInputStream: {
      FX_LOGST(ERROR, kTag) << "Child node cannot be input node";
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
    // GDC
    case NodeType::kGdc: {
      auto gdc_result = camera::GdcNode::CreateGdcNode(
          memory_allocator_, dispatcher_, device_, gdc_, info, parent_node, *next_node_internal);
      if (gdc_result.is_error()) {
        FX_PLOGST(ERROR, kTag, gdc_result.error()) << "Failed to configure GDC Node";
        // TODO(braval): Handle already configured nodes
        return fit::error(gdc_result.error());
      }
      return CreateGraph(info, *next_node_internal, gdc_result.value());
    }
    // GE2D
    case NodeType::kGe2d: {
      auto ge2d_result = camera::Ge2dNode::CreateGe2dNode(
          memory_allocator_, dispatcher_, device_, ge2d_, info, parent_node, *next_node_internal);
      if (ge2d_result.is_error()) {
        FX_PLOGST(ERROR, kTag, ge2d_result.error()) << "Failed to configure GE2D Node";
        // TODO(braval): Handle already configured nodes
        return fit::error(ge2d_result.error());
      }
      return CreateGraph(info, *next_node_internal, ge2d_result.value());
    }
    // Output Node
    case NodeType::kOutputStream: {
      result =
          camera::OutputNode::CreateOutputNode(dispatcher_, info, parent_node, *next_node_internal);
      if (result.is_error()) {
        FX_LOGST(ERROR, kTag) << "Failed to configure Output Node";
        // TODO(braval): Handle already configured nodes
        return result;
      }
      output_nodes_info_[info->stream_type()] = result.value();
      break;
    }
      // clang-format off
    default: {
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
      // clang-format on
  }
  return result;
}

fit::result<std::pair<InternalConfigNode, ProcessNode*>, zx_status_t>
PipelineManager::FindNodeToAttachNewStream(StreamCreationData* info,
                                           const InternalConfigNode& current_internal_node,
                                           ProcessNode* node) {
  auto requested_stream_type = info->stream_type();

  // Validate if this node supports the requested stream type
  // to be safe.
  if (!node->is_stream_supported(requested_stream_type)) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  // Traverse the |node| to find a node which supports this stream
  // but none of its children support this stream.
  for (auto& child_node : node->child_nodes()) {
    if (child_node->is_stream_supported(requested_stream_type)) {
      // If we find a child node which supports the requested stream type,
      // we move on to that child node.
      const auto* next_internal_node =
          GetNextNodeInPipeline(info->stream_type(), current_internal_node);
      if (!next_internal_node) {
        FX_LOGS(ERROR) << "Failed to get next node for requested stream";
        return fit::error(ZX_ERR_INTERNAL);
      }
      return FindNodeToAttachNewStream(info, *next_internal_node, child_node.get());
    }
    // This is the node we need to attach the new stream pipeline to
    return fit::ok(std::make_pair(current_internal_node, node));
  }

  // Should not reach here
  FX_LOGS(ERROR) << "Failed FindNodeToAttachNewStream";
  return fit::error(ZX_ERR_INTERNAL);
}

void PipelineManager::ConfigureStreamPipeline(
    StreamCreationData info, fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "PipelineManager::ConfigureStreamPipeline");
  TRACE_FLOW_BEGIN("camera", "post_configure_stream_pipeline_task", nonce);
  PostTask([this, nonce, info = std::move(info), stream = std::move(stream)]() mutable {
    TRACE_DURATION("camera", "PipelineManager::ConfigureStreamPipeline.task");
    TRACE_FLOW_END("camera", "post_configure_stream_pipeline_task", nonce);
    zx_status_t status = ZX_OK;
    auto cleanup = fbl::MakeAutoCall([this, &stream, &status]() {
      TaskComplete();
      if (status != ZX_OK) {
        stream.Close(status);
      }
    });

    ProcessNode* graph_node_to_be_appended = nullptr;
    camera::InternalConfigNode internal_graph_node_to_be_appended;
    std::unique_ptr<camera::ProcessNode> graph_head;
    fbl::AutoCall shutdown_graph_on_error([&] {
      if (graph_head) {
        graph_head->OnShutdown([] {});
      }
    });

    auto* input_node = FindStream(info.node.input_stream_type);
    if (input_node) {
      // If the same stream is requested again, return an error..
      if (HasStreamType(input_node->configured_streams(),
                        info.stream_config.properties.stream_type())) {
        status = ZX_ERR_ALREADY_BOUND;
        return;
      }
      // Find the node at which the new graph needs to be appended.
      auto result = FindNodeToAttachNewStream(&info, info.node, input_node);
      if (result.is_error()) {
        FX_PLOGS(ERROR, result.error()) << "Failed FindNodeToAttachNewStream";
        status = result.error();
        return;
      }

      // If the next node is an output node, it is currently not supported.
      // Currently the expectation is that the clients will request streams in a fixed order.
      // TODO(42241): Remove this check when 42241 is fixed.
      const auto* next_node_internal =
          GetNextNodeInPipeline(info.stream_type(), result.value().first);
      if (!next_node_internal) {
        FX_LOGS(ERROR) << "Failed to get next node";
        status = ZX_ERR_INTERNAL;
        return;
      }

      if (next_node_internal->type == NodeType::kOutputStream) {
        FX_LOGS(WARNING)
            << "Cannot create this stream due to unexpected ordering of stream create requests";
        status = ZX_ERR_NOT_SUPPORTED;
        return;
      }

      graph_node_to_be_appended = result.value().second;
      internal_graph_node_to_be_appended = result.value().first;
    } else {
      auto input_result =
          camera::InputNode::CreateInputNode(&info, memory_allocator_, dispatcher_, isp_);
      if (input_result.is_error()) {
        FX_PLOGST(ERROR, kTag, input_result.error()) << "Failed to ConfigureInputNode";
        status = input_result.error();
        return;
      }

      graph_head = std::move(input_result.value());
      graph_node_to_be_appended = graph_head.get();
      internal_graph_node_to_be_appended = info.node;
    }

    auto output_node_result =
        CreateGraph(&info, internal_graph_node_to_be_appended, graph_node_to_be_appended);
    if (output_node_result.is_error()) {
      FX_PLOGST(ERROR, kTag, output_node_result.error()) << "Failed to CreateGraph";
      status = output_node_result.error();
      return;
    }

    auto* output_node = output_node_result.value();
    auto stream_configured = info.stream_type();
    status = output_node->Attach(stream.TakeChannel(), [this, stream_configured]() {
      FX_LOGS(DEBUG) << "Stream client disconnected";
      OnClientStreamDisconnect(stream_configured);
    });
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to bind output stream";
      return;
    }

    if (input_node) {
      // Push this new requested stream to all pre-existing nodes |configured_streams| vector.
      auto requested_stream_type = info.stream_type();
      auto* current_node = graph_node_to_be_appended;
      while (current_node) {
        current_node->configured_streams().push_back(requested_stream_type);
        current_node = current_node->parent_node();
      }
    } else {
      status =
          isp_.SetFrameRateRange(reinterpret_cast<const frame_rate_t*>(&info.frame_rate_range.min),
                                 reinterpret_cast<const frame_rate_t*>(&info.frame_rate_range.max));
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to SetFrameRateRange";
        return;
      }
      shutdown_graph_on_error.cancel();
      streams_[info.node.input_stream_type] = std::move(graph_head);
    }
  });
}

static void RemoveStreamType(std::vector<fuchsia::camera2::CameraStreamType>& streams,
                             fuchsia::camera2::CameraStreamType stream_to_remove) {
  auto it = std::find(streams.begin(), streams.end(), stream_to_remove);
  if (it != streams.end()) {
    streams.erase(it);
  }
}

void PipelineManager::TaskComplete() {
  task_in_progress_ = false;
  tasks_event_.signal(0u, kTaskQueued);
}

void PipelineManager::DeleteGraphForDisconnectedStream(
    ProcessNode* graph_head, fuchsia::camera2::CameraStreamType stream_to_disconnect) {
  TRACE_DURATION("camera", "PipelineManager::DeleteGraphForDisconnectedStream", "stream_type",
                 static_cast<uint32_t>(stream_to_disconnect));

  // More than one stream supported by this graph.
  // Check for this nodes children to see if we can find the |stream_to_disconnect|
  // as part of configured_streams.
  auto& child_nodes = graph_head->child_nodes();
  for (auto it = child_nodes.begin(); it != child_nodes.end(); it++) {
    if (HasStreamType(it->get()->configured_streams(), stream_to_disconnect)) {
      if (it->get()->configured_streams().size() == 1) {
        it = child_nodes.erase(it);

        auto* current_node = graph_head;
        while (current_node) {
          // Remove entry from configured streams.
          RemoveStreamType(current_node->configured_streams(), stream_to_disconnect);

          current_node = current_node->parent_node();
        }
        TaskComplete();
        return;
      }
      return DeleteGraphForDisconnectedStream(it->get(), stream_to_disconnect);
    }
  }
}

void PipelineManager::DisconnectStream(ProcessNode* graph_head,
                                       fuchsia::camera2::CameraStreamType input_stream_type,
                                       fuchsia::camera2::CameraStreamType stream_to_disconnect) {
  auto shutdown_callback = [this, input_stream_type, stream_to_disconnect]() {
    TRACE_DURATION("camera", "PipelineManager::DisconnectStream Callback", "stream_type",
                   static_cast<uint32_t>(stream_to_disconnect));

    ProcessNode* graph_head = nullptr;

    // Remove entry from shutdown book keeping.
    output_nodes_info_.erase(stream_to_disconnect);
    stream_shutdown_requested_.erase(
        std::remove(stream_shutdown_requested_.begin(), stream_shutdown_requested_.end(),
                    stream_to_disconnect),
        stream_shutdown_requested_.end());

    // Check if global shutdown was requested and it was complete before exiting
    // this function.
    auto shutdown_cleanup = fbl::MakeAutoCall([this]() {
      if (output_nodes_info_.empty() && global_shutdown_requested_) {
        // Signal for pipeline manager shutdown complete.
        shutdown_event_.signal(0, kPipelineManagerSignalExitDone);
      }
    });

    auto* input_node = FindStream(input_stream_type);
    if (input_node == nullptr) {
      ZX_ASSERT_MSG(false, "Invalid input stream type\n");
      return;
    }

    if (input_node->configured_streams().size() == 1) {
      streams_.erase(input_stream_type);
      TaskComplete();
      return;
    }

    graph_head = input_node;
    DeleteGraphForDisconnectedStream(graph_head, stream_to_disconnect);
  };

  // Only one stream supported by the graph.
  if (graph_head->configured_streams().size() == 1 &&
      HasStreamType(graph_head->configured_streams(), stream_to_disconnect)) {
    graph_head->OnShutdown(shutdown_callback);
    return;
  }

  // More than one stream supported by this graph.
  // Check for this nodes children to see if we can find the |stream_to_disconnect|
  // as part of configured_streams.
  auto& child_nodes = graph_head->child_nodes();
  for (auto& child_node : child_nodes) {
    if (HasStreamType(child_node->configured_streams(), stream_to_disconnect)) {
      return DisconnectStream(child_node.get(), input_stream_type, stream_to_disconnect);
    }
  }
}

fit::result<std::pair<ProcessNode*, fuchsia::camera2::CameraStreamType>, zx_status_t>
PipelineManager::FindGraphHead(fuchsia::camera2::CameraStreamType stream_type) {
  for (auto& stream : streams_) {
    if (HasStreamType(stream.second->configured_streams(), stream_type)) {
      return fit::ok(std::make_pair(stream.second.get(), stream.first));
    }
  }
  return fit::error(ZX_ERR_BAD_STATE);
}

void PipelineManager::OnClientStreamDisconnect(
    fuchsia::camera2::CameraStreamType stream_to_disconnect) {
  PostTask([this, stream_to_disconnect]() {
    TRACE_DURATION("camera", "PipelineManager::OnClientStreamDisconnect", "stream_type",
                   static_cast<uint32_t>(stream_to_disconnect));
    stream_shutdown_requested_.push_back(stream_to_disconnect);

    auto result = FindGraphHead(stream_to_disconnect);
    if (result.is_error()) {
      FX_PLOGS(ERROR, result.error()) << "Failed to FindGraphHead";
      ZX_ASSERT_MSG(false, "Invalid stream_to_disconnect stream type\n");
    }

    DisconnectStream(result.value().first, result.value().second, stream_to_disconnect);
  });
}

void PipelineManager::StopStreaming() {
  for (auto output_node_info : output_nodes_info_) {
    if (output_node_info.second) {
      output_node_info.second->client_stream()->Stop();
    }
  }
}

void PipelineManager::StartStreaming() {
  for (auto output_node_info : output_nodes_info_) {
    if (output_node_info.second) {
      output_node_info.second->client_stream()->Start();
    }
  }
}

void PipelineManager::Shutdown() {
  // No existing streams, safe to signal shutdown complete.
  if (output_nodes_info_.empty()) {
    // Signal for pipeline manager shutdown complete.
    shutdown_event_.signal(0u, kPipelineManagerSignalExitDone);
    return;
  }

  // First stop streaming all active streams.
  StopStreaming();

  // Instantiate the shutdown of all active streams before transitioning the state to
  // "global_shutdown_requested".
  auto output_node_info_copy = output_nodes_info_;
  for (auto output_node_info : output_node_info_copy) {
    if (!HasStreamType(stream_shutdown_requested_, output_node_info.first)) {
      OnClientStreamDisconnect(output_node_info.first);
    }
  }

  // Transition the state to ensure that no new tasks are posted on the task queue.
  global_shutdown_requested_ = true;
}

void PipelineManager::SetupTaskWaiter() {
  ZX_ASSERT_MSG(ZX_OK == zx::event::create(0, &tasks_event_), "Failed to create a task event");

  tasks_event_waiter_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
    TRACE_DURATION("camera", "PipelineManager::TaskWaiter");
    // Clear the signal.
    tasks_event_.signal(kTaskQueued, 0u);

    if (!task_in_progress_ && !task_queue_.empty()) {
      task_in_progress_ = true;
      auto task = std::move(task_queue_.front());
      task_queue_.pop();
      async::PostTask(dispatcher_, std::move(task));
    }

    tasks_event_waiter_.Begin(dispatcher_);
  });

  tasks_event_waiter_.set_object(tasks_event_.get());
  tasks_event_waiter_.set_trigger(kTaskQueued);
  tasks_event_waiter_.Begin(dispatcher_);
}

void PipelineManager::PostTask(fit::closure task) {
  if (global_shutdown_requested_) {
    FX_LOGS(DEBUG) << "Global shutdown requested, ignoring the task posted on the task queue";
    return;
  }
  task_queue_.emplace(std::move(task));
  tasks_event_.signal(0u, kTaskQueued);
}

}  // namespace camera
