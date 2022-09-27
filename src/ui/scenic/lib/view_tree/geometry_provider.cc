// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "geometry_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <stack>

#include <measure_tape/hlcpp/measure_tape_for_geometry.h>

#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

namespace view_tree {
using fuog_ViewTreeWatcher = fuchsia::ui::observation::geometry::ViewTreeWatcher;
using fuog_ViewTreeSnapshot = fuchsia::ui::observation::geometry::ViewTreeSnapshot;
using fuog_ViewTreeSnapshotPtr = fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr;
using fuog_WatchResponse = fuchsia::ui::observation::geometry::WatchResponse;
using fuog_ViewDescriptor = fuchsia::ui::observation::geometry::ViewDescriptor;
using fuog_Layout = fuchsia::ui::observation::geometry::Layout;
using fuog_RotatableExtent = fuchsia::ui::observation::geometry::RotatableExtent;
using fuog_Error = fuchsia::ui::observation::geometry::Error;
namespace fuog_measure_tape = measure_tape::fuchsia::ui::observation::geometry;
const auto fuog_BUFFER_SIZE = fuchsia::ui::observation::geometry::BUFFER_SIZE;
const auto fuog_MAX_VIEW_COUNT = fuchsia::ui::observation::geometry::MAX_VIEW_COUNT;

void GeometryProvider::Register(fidl::InterfaceRequest<fuog_ViewTreeWatcher> endpoint,
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

void GeometryProvider::RegisterGlobalViewTreeWatcher(
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::ViewTreeWatcher> endpoint) {
  FX_DCHECK(endpoint.is_valid()) << "precondition";
  auto endpoint_id = endpoint_counter_++;
  endpoints_.insert(
      {endpoint_id, ProviderEndpoint(std::move(endpoint), /*context_view*/ std::nullopt,
                                     endpoint_id, [this, endpoint_id] {
                                       auto count = endpoints_.erase(endpoint_id);
                                       FX_DCHECK(count > 0);
                                     })});
}

void GeometryProvider::OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
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

fuog_ViewTreeSnapshotPtr GeometryProvider::ExtractObservationSnapshot(
    std::optional<zx_koid_t> endpoint_context_view,
    std::shared_ptr<const view_tree::Snapshot> snapshot) {
  auto view_tree_snapshot = fuog_ViewTreeSnapshot::New();
  view_tree_snapshot->set_time(scenic_impl::gfx::dispatcher_clock_now());
  std::vector<fuog_ViewDescriptor> views;
  bool views_exceeded = false;

  // |ProviderEndpoint| not having a |context_view_| get global access to the view tree as they get
  // registered through f.u.o.t.Registry.RegisterGlobalViewTreeWatcher.
  zx_koid_t context_view = 0;
  if (endpoint_context_view.has_value()) {
    context_view = endpoint_context_view.value();
  } else {
    context_view = snapshot->root;
  }

  // Empty snapshot case.
  if (context_view == ZX_KOID_INVALID && snapshot->view_tree.empty()) {
    view_tree_snapshot->set_views({});
    return view_tree_snapshot;
  }

  // It is possible that |context_view| has not yet connected to the view tree or it has
  // disconnected. In either case, send an empty snapshot.
  if (snapshot->view_tree.count(context_view) == 0) {
    view_tree_snapshot->set_views({});
    return view_tree_snapshot;
  }

  // Perform a depth-first search on the view tree to populate |views| with
  // fuog_ViewDescriptors.
  std::stack<zx_koid_t> stack;
  std::unordered_set<zx_koid_t> visited;
  stack.push(context_view);
  while (!stack.empty()) {
    auto view_node = stack.top();
    stack.pop();
    FX_DCHECK(visited.count(view_node) == 0) << "Cycle detected in the view tree";
    visited.insert(view_node);
    const auto& view = snapshot->view_tree.at(view_node);

    // Do not set a view vector in the |ViewTreeSnapshot| as the size of |views| will exceed
    // fuog_MAX_VIEW_COUNT, since the number of |children| of the |view_node| exceeds
    // fuog_MAX_VIEW_COUNT.
    if (view.children.size() > fuog_MAX_VIEW_COUNT) {
      views_exceeded = true;
      break;
    }

    for (auto child : view.children) {
      stack.push(child);
    }

    // Add |ViewDescriptor|s for nodes created by flatland instances in |views|. For GFX instances,
    // add the |ViewDescriptor|s for nodes that have generated the |is_rendering| signal.
    const bool is_flatland_view = !view.gfx_is_rendering.has_value();
    const bool gfx_rendered_view =
        view.gfx_is_rendering.has_value() && view.gfx_is_rendering.value();
    if (is_flatland_view || gfx_rendered_view) {
      views.push_back(ExtractViewDescriptor(view_node, context_view, snapshot));
    }

    // Do not set a view vector in the |ViewTreeSnapshot| as the size of |views| will exceed
    // fuog_MAX_VIEW_COUNT, since the stack is not empty.
    if (views.size() == fuog_MAX_VIEW_COUNT && stack.size() > 0) {
      views_exceeded = true;
      break;
    }
  }

  if (!views_exceeded) {
    view_tree_snapshot->set_views(std::move(views));
  }
  return view_tree_snapshot;
}

fuog_ViewDescriptor GeometryProvider::ExtractViewDescriptor(
    zx_koid_t view_ref_koid, zx_koid_t context_view,
    std::shared_ptr<const view_tree::Snapshot> snapshot) {
  auto& view_node = snapshot->view_tree.at(view_ref_koid);

  std::array<float, 2> pixel_scale = utils::kDefaultPixelScale;
  if (view_node.gfx_pixel_scale.has_value()) {
    pixel_scale = view_node.gfx_pixel_scale.value();
  }

  fuchsia::math::InsetF inset;
  if (view_node.gfx_inset.has_value()) {
    inset = view_node.gfx_inset.value();
  }

  // The coordinates of a view_node's bounding box.
  fuog_Layout layout = {
      .extent = {.min = {view_node.bounding_box.min[0], view_node.bounding_box.min[1]},
                 .max = {view_node.bounding_box.max[0], view_node.bounding_box.max[1]}},
      .pixel_scale = std::move(pixel_scale),
      .inset = std::move(inset)};

  auto world_from_local_transform = glm::inverse(view_node.local_from_world_transform);
  auto extent_in_context_transform =
      snapshot->view_tree.at(context_view).local_from_world_transform * world_from_local_transform;

  // The coordinates of a view_node's bounding box in context_view's coordinate system.
  auto extent_in_context_top_left = utils::TransformPointerCoords(
      {view_node.bounding_box.min[0], view_node.bounding_box.min[1]}, extent_in_context_transform);
  auto extent_in_context_top_right = utils::TransformPointerCoords(
      {view_node.bounding_box.max[0], view_node.bounding_box.min[1]}, extent_in_context_transform);
  auto extent_in_context_bottom_left = utils::TransformPointerCoords(
      {view_node.bounding_box.min[0], view_node.bounding_box.max[1]}, extent_in_context_transform);

  auto extent_in_context_dx = extent_in_context_top_right[0] - extent_in_context_top_left[0];
  auto extent_in_context_dy = extent_in_context_top_right[1] - extent_in_context_top_left[1];

  // TODO(fxb/92869) : Handle floating point precision errors in calculating the angle.
  // Angle of a line segment with coordinates (x1,y1) and (x2,y2) is defined as tan inverse
  // (y2-y1/x2-x1). As the return value is in radians multiply it by 180/PI.
  FX_DCHECK(extent_in_context_dx != 0 || extent_in_context_dy != 0)
      << "top left and top right coordinates cannot be the same";
  auto angle_context = atan2(extent_in_context_dy, extent_in_context_dx) * (180. / M_PI);

  // Change the range of |angle_context| from [-pi,pi] to [0,2*pi).
  angle_context = std::fmod(angle_context + 360, 360);

  fuog_RotatableExtent extent_in_context = {
      .origin = {extent_in_context_top_left[0], extent_in_context_top_left[1]},
      // Root mean squared distance between two coordinates.
      .width = std::hypot(extent_in_context_top_right[0] - extent_in_context_top_left[0],
                          extent_in_context_top_right[1] - extent_in_context_top_left[1]),
      .height = std::hypot(extent_in_context_bottom_left[0] - extent_in_context_top_left[0],
                           extent_in_context_bottom_left[1] - extent_in_context_top_left[1]),
      .angle_degrees = static_cast<float>(angle_context)};

  glm::mat4 extent_in_parent_transform;

  // If the context view is the root node, it will not have a parent so the
  // extent_in_parent_transform will be an identity matrix.
  if (view_node.parent != ZX_KOID_INVALID) {
    extent_in_parent_transform =
        snapshot->view_tree.at(view_node.parent).local_from_world_transform *
        world_from_local_transform;
  }

  // The coordinates of a view_node's bounding box in its parent's coordinate system.
  auto extent_in_parent_top_left = utils::TransformPointerCoords(
      {view_node.bounding_box.min[0], view_node.bounding_box.min[1]}, extent_in_parent_transform);
  auto extent_in_parent_top_right = utils::TransformPointerCoords(
      {view_node.bounding_box.max[0], view_node.bounding_box.min[1]}, extent_in_parent_transform);
  auto extent_in_parent_bottom_left = utils::TransformPointerCoords(
      {view_node.bounding_box.min[0], view_node.bounding_box.max[1]}, extent_in_parent_transform);

  auto extent_in_parent_dx = extent_in_parent_top_right[0] - extent_in_parent_top_left[0];
  auto extent_in_parent_dy = extent_in_parent_top_right[1] - extent_in_parent_top_left[1];
  FX_DCHECK(extent_in_parent_dx != 0 || extent_in_parent_dy != 0)
      << "top left and top right coordinates cannot be the same";

  // TODO(fxb/92869) : Handle floating point precision errors in calculating the angle.
  auto angle_parent = atan2(extent_in_parent_dy, extent_in_parent_dx) * (180. / M_PI);

  // Change the range of |angle_parent| from [-pi,pi] to [0,2*pi).
  angle_parent = std::fmod(angle_parent + 360, 360);

  fuog_RotatableExtent extent_in_parent = {
      .origin = {extent_in_parent_top_left[0], extent_in_parent_top_left[1]},
      .width = std::hypot(extent_in_parent_top_right[0] - extent_in_parent_top_left[0],
                          extent_in_parent_top_right[1] - extent_in_parent_top_left[1]),
      .height = std::hypot(extent_in_parent_bottom_left[0] - extent_in_parent_top_left[0],
                           extent_in_parent_bottom_left[1] - extent_in_parent_top_left[1]),
      .angle_degrees = static_cast<float>(angle_parent)};

  fuog_ViewDescriptor view_descriptor;
  view_descriptor.set_view_ref_koid(view_ref_koid);
  view_descriptor.set_layout(std::move(layout));
  view_descriptor.set_extent_in_context(std::move(extent_in_context));
  view_descriptor.set_extent_in_parent(std::move(extent_in_parent));

  FX_DCHECK(view_node.children.size() <= fuog_MAX_VIEW_COUNT) << "invariant.";
  view_descriptor.set_children({view_node.children.begin(), view_node.children.end()});

  return view_descriptor;
}

GeometryProvider::ProviderEndpoint::ProviderEndpoint(
    fidl::InterfaceRequest<fuog_ViewTreeWatcher> endpoint, std::optional<zx_koid_t> context_view,
    ProviderEndpointId id, fit::function<void()> destroy_instance_function)
    : endpoint_(this, std::move(endpoint)),
      context_view_(std::move(context_view)),
      id_(id),
      // The |destroy_instance_function_| captures the 'this' pointer to the GeometryProvider
      // instance. However, the below operation is safe because it is expected that the
      // GeometryProvider outlives the ProviderEndpoint.
      destroy_instance_function_(std::move(destroy_instance_function)) {}

GeometryProvider::ProviderEndpoint::ProviderEndpoint(ProviderEndpoint&& original) noexcept
    : endpoint_(this, original.endpoint_.Unbind()),
      view_tree_snapshots_(std::move(original.view_tree_snapshots_)),
      pending_callback_(std::move(original.pending_callback_)),
      context_view_(original.context_view_),
      id_(original.id_),
      destroy_instance_function_(std::move(original.destroy_instance_function_)) {}

void GeometryProvider::ProviderEndpoint::AddViewTreeSnapshot(
    fuog_ViewTreeSnapshotPtr view_tree_snapshot) {
  view_tree_snapshots_.push_back(std::move(view_tree_snapshot));

  if (view_tree_snapshots_.size() > fuog_BUFFER_SIZE) {
    view_tree_snapshots_.pop_front();
    error_ |= fuchsia::ui::observation::geometry::Error::BUFFER_OVERFLOW;
  }
  FX_DCHECK(view_tree_snapshots_.size() <= fuog_BUFFER_SIZE) << "invariant";

  SendResponseMaybe();
}

void GeometryProvider::ProviderEndpoint::Watch(fuog_ViewTreeWatcher::WatchCallback callback) {
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

void GeometryProvider::ProviderEndpoint::SendResponseMaybe() {
  // Check if we have a client waiting for a response and if we have snapshots queued up to be sent
  // to the client before sending the response.
  if (pending_callback_ != nullptr && !view_tree_snapshots_.empty()) {
    SendResponse();
  }
}

void GeometryProvider::ProviderEndpoint::SendResponse() {
  FX_DCHECK(!view_tree_snapshots_.empty());
  FX_DCHECK(pending_callback_ != nullptr);

  fuog_WatchResponse watch_response;
  watch_response.set_epoch_end(scenic_impl::gfx::dispatcher_clock_now());

  int64_t response_size = fuog_measure_tape::Measure(watch_response).num_bytes;

  // Send pending snapshots to the client in a chronological order and clear the deque. If the size
  // of the response exceeds ZX_CHANNEL_MAX_MSG_BYTES, drop the oldest fuog_ViewTreeSnapshot in the
  // response.
  while (!view_tree_snapshots_.empty() && response_size < ZX_CHANNEL_MAX_MSG_BYTES) {
    response_size += fuog_measure_tape::Measure(*view_tree_snapshots_.back()).num_bytes;
    if (response_size < ZX_CHANNEL_MAX_MSG_BYTES) {
      // The absence of a views vector in |ViewTreeSnapshot| indicates that a view overflow has
      // occurred.
      if (!view_tree_snapshots_.back()->has_views()) {
        error_ |= fuchsia::ui::observation::geometry::Error::VIEWS_OVERFLOW;
      }
      watch_response.mutable_updates()->push_back(std::move(*view_tree_snapshots_.back()));
      view_tree_snapshots_.pop_back();
    } else {
      error_ |= fuchsia::ui::observation::geometry::Error::CHANNEL_OVERFLOW;
    }
  }

  std::reverse(watch_response.mutable_updates()->begin(), watch_response.mutable_updates()->end());

  if (error_) {
    watch_response.set_error(error_);
  }

  pending_callback_(std::move(watch_response));

  // Clear the pending_callback_ and reset the state for subsequent Watch() calls.
  Reset();
}

void GeometryProvider::ProviderEndpoint::CloseChannel() {
  endpoint_.Close(ZX_ERR_BAD_STATE);
  // NOTE: Triggers destruction of this object.
  destroy_instance_function_();
}

void GeometryProvider::ProviderEndpoint::Reset() {
  pending_callback_ = nullptr;
  view_tree_snapshots_.clear();
  error_ = error_ ^ error_;  // Clear bits
}

}  // namespace view_tree
