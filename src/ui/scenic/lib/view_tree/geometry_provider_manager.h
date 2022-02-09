// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_GEOMETRY_PROVIDER_MANAGER_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_GEOMETRY_PROVIDER_MANAGER_H_

#include <fuchsia/ui/observation/geometry/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <deque>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree {
// This class is responsible for registering and maintaining server endpoints for
// fuchsia.ui.observation.geometry.Provider protocol clients. This class also listens for new
// snapshots generated every frame, and sends a processed version of them to these registered
// clients.
class GeometryProviderManager {
 public:
  GeometryProviderManager() = default;
  // Adds a server side endpoint to |endpoints_| for lifecycle management.
  void Register(fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> endpoint,
                zx_koid_t context_view);

  // Adds a server side endpoint provided by
  // fuchsia.ui.observation.test.Registry.RegisterGlobalGeometryProvider to |endpoints_|. Endpoints
  // registered by this method get a global access to the view tree.
  void RegisterGlobalGeometryProvider(
      fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> endpoint);

  // Inject a new snapshot of the ViewTree. Adds the snapshot to each ProviderEndpoint's
  // |view_tree_snapshots_| and send a response to the respective clients if the required conditions
  // are met. See SendResponseMaybe() for more details on the required conditions.
  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot);

  // Generates a fuchsia.ui.observation.geometry.ViewTreeSnapshot from the |snapshot| by
  // extracting information about the |context_view| and its descendant views from
  // |snapshot|.
  static fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr ExtractObservationSnapshot(
      std::optional<zx_koid_t> endpoint_context_view,
      std::shared_ptr<const view_tree::Snapshot> snapshot);

 private:
  using ProviderEndpointId = int64_t;

  // This class implements the server side endpoint for fuchsia.ui.observation.geometry.Provider
  // clients and manages a deque of snapshot updates to be sent to the client on receiving a Watch()
  // call.
  class ProviderEndpoint : public fuchsia::ui::observation::geometry::Provider {
   public:
    explicit ProviderEndpoint(
        fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> provider,
        std::optional<zx_koid_t> context_view, ProviderEndpointId id,
        fit::function<void()> destroy_instance_function);

    ProviderEndpoint(ProviderEndpoint&& original) noexcept;

    // |fuchsia.ui.observation.geometry.Provider.Watch|.
    void Watch(fuchsia::ui::observation::geometry::Provider::WatchCallback callback) override;

    // Adds the latest snapshot to |view_tree_snapshots_|.
    //
    // If the size of |view_tree_snapshots_| exceeds |fuchsia.ui.observation.geometry.BUFFER_SIZE|,
    // it replaces the oldest snapshot with the new one. If there were any pending callback because
    // of a client calling Watch() when there were no pending snapshots, it gets triggered with the
    // latest |view_tree_snapshots_|.
    void AddViewTreeSnapshot(
        fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr view_tree_snapshot);

    bool IsAlive() const { return endpoint_.is_bound(); }

    std::optional<zx_koid_t> context_view() const { return context_view_; }

   private:
    // Checks whether the required conditions for sending the response to the client are met and
    // then sends the response.
    void SendResponseMaybe();

    // Trigger the |pending_callback_| to send the response to the client. If the size of the
    // response exceeds ZX_CHANNEL_MAX_MSG_BYTES, older
    // `fuchsia.ui.observation.geometry.ViewTreeSnapshot`s in the response are dropped.
    void SendResponse();

    // Closes the fidl channel. This triggers the destruction of the ProviderEndpoint object through
    // the |destroy_instance_function_|. NOTE: No further method calls or member accesses should be
    // made after CloseChannel(), since they might be made on a destroyed object.
    void CloseChannel();

    // Resets the state of an |endpoint_| for subsequent |Watch| calls.
    void Reset();

    // Server-side endpoint.
    fidl::Binding<fuchsia::ui::observation::geometry::Provider> endpoint_;

    // A deque containing pending snapshot updates for a client. The size of the deque cannot exceed
    // |fuchsia::ui::observation::geometry::BUFFER_SIZE|.
    std::deque<fuchsia::ui::observation::geometry::ViewTreeSnapshotPtr> view_tree_snapshots_;

    // If the last |Watch| call did not immediately trigger a callback, it gets stored here and is
    // triggered whenever a new snapshot gets generated.
    fuchsia::ui::observation::geometry::Provider::WatchCallback pending_callback_;

    std::optional<const zx_koid_t> context_view_;

    // Key for storing the associated server endpoint in |endpoints_|.
    const ProviderEndpointId id_;

    // A closure which gets triggered whenever the server endpoint closes. The closure is
    // responsible for removing the ProviderEndpoint from |endpoints_|.
    fit::function<void()> destroy_instance_function_;

    // Errors faced while executing the |pending_callback_|. |error_| must be reset after
    // |pending_callback_| is executed for subsequent |Watch| calls.
    fuchsia::ui::observation::geometry::Error error_;
  };

  // Generates a fuchsia.ui.observation.geometry.ViewDescriptor from the |snapshot|'s view node by
  // extracting information about the |view_ref_koid| from the view node.
  static fuchsia::ui::observation::geometry::ViewDescriptor ExtractViewDescriptor(
      zx_koid_t view_ref_koid, zx_koid_t context_view,
      std::shared_ptr<const view_tree::Snapshot> snapshot);

  std::unordered_map<ProviderEndpointId, ProviderEndpoint> endpoints_;

  // Incremented when Register() is called.
  ProviderEndpointId endpoint_counter_ = 0;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(GeometryProviderManager);
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_GEOMETRY_PROVIDER_MANAGER_H_
