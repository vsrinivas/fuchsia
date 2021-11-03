// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "geometry_provider_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace view_tree {
using fuog_Provider = fuchsia::ui::observation::geometry::Provider;
using fuog_ViewTreeSnapshot = fuchsia::ui::observation::geometry::ViewTreeSnapshot;
using fuog_ViewTreeSnapshotPtr = fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr;
using fuog_Provider_Watch_Result = fuchsia::ui::observation::geometry::Provider_Watch_Result;
using fuog_Provider_Watch_Response = fuchsia::ui::observation::geometry::Provider_Watch_Response;
const auto fuog_BUFFER_SIZE = fuchsia::ui::observation::geometry::BUFFER_SIZE;

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
  // TODO(fxbug.dev/84801): Implement the logic to convert a view_tree:Snapshot to ViewTreeSnapshot
  // here. Currently returning a unique ptr to an empty ViewTreeSnapshot.
  return fuog_ViewTreeSnapshot::New();
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

  fuog_Provider_Watch_Response watch_result;
  watch_result.epoch_end = zx::clock::get_monotonic().get();

  // Send pending snapshots to the client in a chronological order and clear the deque.
  while (!view_tree_snapshots_.empty()) {
    watch_result.updates.push_back(std::move(*view_tree_snapshots_.front()));
    view_tree_snapshots_.pop_front();
  }

  pending_callback_(fuog_Provider_Watch_Result::WithResponse(std::move(watch_result)));

  // Clear the pending_callback_ so that the client can make subsequent Watch() calls.
  pending_callback_ = nullptr;
}

void GeometryProviderManager::ProviderEndpoint::CloseChannel() {
  endpoint_.Close(ZX_ERR_BAD_STATE);
  // NOTE: Triggers destruction of this object.
  destroy_instance_function_();
}

}  // namespace view_tree
