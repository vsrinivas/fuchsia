// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "geometry_provider_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <stack>

#include <measure_tape/hlcpp/measure_tape_for_geometry.h>

#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/utils/math.h"

namespace view_tree {
using fuog_Provider = fuchsia::ui::observation::geometry::Provider;
using fuog_ViewTreeSnapshot = fuchsia::ui::observation::geometry::ViewTreeSnapshot;
using fuog_ViewTreeSnapshotPtr = fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr;
using fuog_ProviderWatchResponse = fuchsia::ui::observation::geometry::ProviderWatchResponse;
using fuog_ViewDescriptor = fuchsia::ui::observation::geometry::ViewDescriptor;
using fuog_Layout = fuchsia::ui::observation::geometry::Layout;
using fuog_RotatableExtent = fuchsia::ui::observation::geometry::RotatableExtent;
namespace fuog_measure_tape = measure_tape::fuchsia::ui::observation::geometry;
const auto fuog_BUFFER_SIZE = fuchsia::ui::observation::geometry::BUFFER_SIZE;
const auto fuog_MAX_VIEW_COUNT = fuchsia::ui::observation::geometry::MAX_VIEW_COUNT;

void GeometryProviderManager::Register(fidl::InterfaceRequest<fuog_Provider> endpoint,
                                       zx_koid_t context_view) {
  FX_DCHECK(endpoint.is_valid()) << "precondition";
  FX_DCHECK(context_view != ZX_KOID_INVALID) << "precondition";

  auto endpoint_id = endpoint_counter_++;
  endpoints_.insert({endpoint_id, ProviderEndpoint(std::move(endpoint), context_view, endpoint_id,
                                                   [this, endpoint_id] {
                                                     auto count = endpoints_.erase(endpoint_id);
                                                     FX_DCHECK(count > 0);
                                                   })});
}

void GeometryProviderManager::OnNewViewTreeSnapshot(
    std::shared_ptr<const view_tree::Snapshot> snapshot) {
  // Remove any dead endpoints.
  for (auto it = endpoints_.begin(); it != endpoints_.end();) {
    if (!it->second.IsAlive()) {
      it = endpoints_.erase(it);
    } else {
      ++it;
    }
  }

  // Add snapshot to each endpoint's buffer.
  for (auto& [_, endpoint] : endpoints_) {
    endpoint.AddViewTreeSnapshot(ExtractObservationSnapshot(endpoint.context_view(), snapshot));
  }
}

fuog_ViewTreeSnapshotPtr GeometryProviderManager::ExtractObservationSnapshot(
    zx_koid_t context_view, std::shared_ptr<const view_tree::Snapshot> snapshot) {
  auto view_tree_snapshot = fuog_ViewTreeSnapshot::New();
  view_tree_snapshot->set_time(scenic_impl::gfx::dispatcher_clock_now());
  std::vector<fuog_ViewDescriptor> views;

  // Perform a depth-first search on the view tree to populate |views| with
  // fuog_ViewDescriptors.
  FX_DCHECK(snapshot->view_tree.count(context_view) > 0) << "precondition";
  std::stack<zx_koid_t> stack;
  std::unordered_set<zx_koid_t> visited;
  stack.push(context_view);
  while (!stack.empty()) {
    auto view_node = stack.top();
    stack.pop();
    FX_DCHECK(visited.count(view_node) == 0) << "Cycle detected in the view tree";
    visited.insert(view_node);
    for (auto child : snapshot->view_tree.at(view_node).children) {
      stack.push(child);
    }
    views.push_back(ExtractViewDescriptor(view_node, context_view, snapshot));

    // Set |view_tree_snapshot|'s incomplete flag to true as the size of |views| will exceed
    // fuog_MAX_VIEW_COUNT, since the stack is not empty.
    if (views.size() == fuog_MAX_VIEW_COUNT && stack.size() > 0) {
      view_tree_snapshot->set_incomplete(true);
      break;
    }
  }

  if (!view_tree_snapshot->has_incomplete()) {
    view_tree_snapshot->set_views(std::move(views));
    view_tree_snapshot->set_incomplete(false);
  }
  return view_tree_snapshot;
}

// TODO(fxb/87579) : Complete the implementation of ExtractViewDescriptor.
fuog_ViewDescriptor GeometryProviderManager::ExtractViewDescriptor(
    zx_koid_t view_ref_koid, zx_koid_t context_view,
    std::shared_ptr<const view_tree::Snapshot> snapshot) {
  auto& view_node = snapshot->view_tree.at(view_ref_koid);
  auto world_from_local_transform = glm::inverse(view_node.local_from_world_transform);
  auto extent_in_context_transform =
      snapshot->view_tree.at(context_view).local_from_world_transform * world_from_local_transform;

  glm::mat4 extent_in_parent_transform;
  if (view_node.parent != ZX_KOID_INVALID) {
    extent_in_parent_transform =
        snapshot->view_tree.at(view_node.parent).local_from_world_transform *
        world_from_local_transform;
  }

  auto [min_x, min_y] = view_node.bounding_box.min;
  auto [max_x, max_y] = view_node.bounding_box.max;

  auto extent_in_context_min_vec =
      utils::TransformPointerCoords({min_x, min_y}, extent_in_context_transform);
  auto extent_in_context_max_vec =
      utils::TransformPointerCoords({max_x, max_y}, extent_in_context_transform);
  auto extent_in_parent_min_vec =
      utils::TransformPointerCoords({min_x, min_y}, extent_in_parent_transform);
  auto extent_in_parent_max_vec =
      utils::TransformPointerCoords({max_x, max_y}, extent_in_parent_transform);

  fuog_Layout layout = {
      .extent = {.min = {extent_in_context_min_vec[0], extent_in_context_min_vec[1]},
                 .max = {extent_in_context_max_vec[0], extent_in_context_max_vec[1]}},
      .pixel_scale = {1.0, 1.0},
      .inset = {}};

  fuog_RotatableExtent extent_in_context = {
      .origin = {extent_in_context_min_vec[0], extent_in_context_min_vec[1]},
      .width = extent_in_context_max_vec[0] - extent_in_context_min_vec[0],
      .height = extent_in_context_max_vec[1] - extent_in_context_min_vec[1],
      .angle = 0};

  fuog_RotatableExtent extent_in_parent = {
      .origin = {extent_in_parent_min_vec[0], extent_in_parent_min_vec[1]},
      .width = extent_in_parent_max_vec[0] - extent_in_parent_min_vec[0],
      .height = extent_in_parent_max_vec[1] - extent_in_parent_min_vec[1],
      .angle = 0};

  std::vector<uint32_t> children;
  for (auto child : view_node.children) {
    if (children.size() < fuog_MAX_VIEW_COUNT) {
      children.push_back(static_cast<uint32_t>(child));
    } else {
      break;
    }
  }

  fuog_ViewDescriptor view_descriptor;
  view_descriptor.set_view_ref_koid(view_ref_koid);
  view_descriptor.set_layout(std::move(layout));
  view_descriptor.set_extent_in_context(std::move(extent_in_context));
  view_descriptor.set_extent_in_parent(std::move(extent_in_parent));
  view_descriptor.set_children(std::move(children));

  return view_descriptor;
}

GeometryProviderManager::ProviderEndpoint::ProviderEndpoint(
    fidl::InterfaceRequest<fuog_Provider> endpoint, zx_koid_t context_view, ProviderEndpointId id,
    fit::function<void()> destroy_instance_function)
    : endpoint_(this, std::move(endpoint)),
      context_view_(context_view),
      id_(id),
      // The |destroy_instance_function_| captures the 'this' pointer to the GeometryProviderManager
      // instance. However, the below operation is safe because it is expected that the
      // GeometryProviderManager outlives the ProviderEndpoint.
      destroy_instance_function_(std::move(destroy_instance_function)) {}

GeometryProviderManager::ProviderEndpoint::ProviderEndpoint(ProviderEndpoint&& original) noexcept
    : endpoint_(this, original.endpoint_.Unbind()),
      view_tree_snapshots_(std::move(original.view_tree_snapshots_)),
      pending_callback_(std::move(original.pending_callback_)),
      context_view_(original.context_view_),
      id_(original.id_),
      destroy_instance_function_(std::move(original.destroy_instance_function_)) {}

void GeometryProviderManager::ProviderEndpoint::AddViewTreeSnapshot(
    fuog_ViewTreeSnapshotPtr view_tree_snapshot) {
  view_tree_snapshots_.push_back(std::move(view_tree_snapshot));

  if (view_tree_snapshots_.size() > fuog_BUFFER_SIZE) {
    view_tree_snapshots_.pop_front();
    old_snapshots_dropped_ = true;
  }
  FX_DCHECK(view_tree_snapshots_.size() <= fuog_BUFFER_SIZE) << "invariant";

  SendResponseMaybe();
}

void GeometryProviderManager::ProviderEndpoint::Watch(fuog_Provider::WatchCallback callback) {
  // Check if there is an ongoing Watch call. If there is an in-flight Watch call, close the channel
  // and remove itself from |endpoints_|.
  if (pending_callback_ != nullptr) {
    CloseChannel();
    return;
  }

  // If there are snapshots lined up in the queue, send the response otherwise store the callback.
  pending_callback_ = std::move(callback);

  SendResponseMaybe();
}

void GeometryProviderManager::ProviderEndpoint::SendResponseMaybe() {
  // Check if we have a client waiting for a response and if we have snapshots queued up to be sent
  // to the client before sending the response.
  if (pending_callback_ != nullptr && !view_tree_snapshots_.empty()) {
    SendResponse();
  }
}

void GeometryProviderManager::ProviderEndpoint::SendResponse() {
  FX_DCHECK(!view_tree_snapshots_.empty());
  FX_DCHECK(pending_callback_ != nullptr);

  fuog_ProviderWatchResponse watch_response;
  watch_response.set_epoch_end(scenic_impl::gfx::dispatcher_clock_now());

  int64_t response_size = sizeof(watch_response) + sizeof(watch_response.epoch_end()) +
                          sizeof(watch_response.updates()) + sizeof(watch_response.is_complete());

  // Send pending snapshots to the client in a chronological order and clear the deque. If the size
  // of the response exceeds ZX_CHANNEL_MAX_MSG_BYTES, drop the oldest fuog_ViewTreeSnapshot in the
  // response.
  while (!view_tree_snapshots_.empty() && response_size < ZX_CHANNEL_MAX_MSG_BYTES) {
    response_size += fuog_measure_tape::Measure(*view_tree_snapshots_.back()).num_bytes;
    if (response_size < ZX_CHANNEL_MAX_MSG_BYTES) {
      watch_response.mutable_updates()->push_back(std::move(*view_tree_snapshots_.back()));
      view_tree_snapshots_.pop_back();
    } else {
      old_snapshots_dropped_ = true;
    }
  }
  view_tree_snapshots_.clear();

  std::reverse(watch_response.mutable_updates()->begin(), watch_response.mutable_updates()->end());

  // Notify the client that the response is incomplete if old snapshots were dropped.
  watch_response.set_is_complete(!old_snapshots_dropped_);
  pending_callback_(std::move(watch_response));

  // Clear the pending_callback_ so that the client can make subsequent Watch() calls.
  pending_callback_ = nullptr;
  old_snapshots_dropped_ = false;
}

void GeometryProviderManager::ProviderEndpoint::CloseChannel() {
  endpoint_.Close(ZX_ERR_BAD_STATE);
  // NOTE: Triggers destruction of this object.
  destroy_instance_function_();
}

}  // namespace view_tree
