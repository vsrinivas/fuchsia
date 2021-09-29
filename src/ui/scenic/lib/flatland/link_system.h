// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
#include <glm/mat3x3.hpp>
// clang-format on

#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

namespace flatland {

using LinkProtocolErrorCallback = std::function<void(const std::string&)>;

// An implementation of the ParentViewportWatcher protocol, consisting of hanging gets for various
// updateable pieces of information.
class ParentViewportWatcherImpl : public fuchsia::ui::composition::ParentViewportWatcher {
 public:
  explicit ParentViewportWatcherImpl(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                                     fidl::InterfaceRequest<ParentViewportWatcher> request,
                                     LinkProtocolErrorCallback error_callback)
      : binding_(this, std::move(request), dispatcher_holder->dispatcher()),
        error_callback_(std::move(error_callback)),
        layout_helper_(dispatcher_holder),
        status_helper_(std::move(dispatcher_holder)) {}

  void UpdateLayoutInfo(fuchsia::ui::composition::LayoutInfo info) {
    layout_helper_.Update(std::move(info));
  }

  void UpdateLinkStatus(fuchsia::ui::composition::ParentViewportStatus status) {
    status_helper_.Update(std::move(status));
  }

  // |fuchsia::ui::composition::ParentViewportWatcher|
  void GetLayout(GetLayoutCallback callback) override {
    if (layout_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetLayout() called when there is a pending GetLayout() call. Flatland connection "
          "will be closed because of broken flow control.");
      return;
    }

    layout_helper_.SetCallback(std::move(callback));
  }

  // |fuchsia::ui::composition::ParentViewportWatcher|
  void GetStatus(GetStatusCallback callback) override {
    if (status_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetStatus() called when there is a pending GetStatus() call. Flatland connection "
          "will be closed because of broken flow control.");
      return;
    }

    status_helper_.SetCallback(std::move(callback));
  }

 private:
  fidl::Binding<fuchsia::ui::composition::ParentViewportWatcher> binding_;
  LinkProtocolErrorCallback error_callback_;
  HangingGetHelper<fuchsia::ui::composition::LayoutInfo> layout_helper_;
  HangingGetHelper<fuchsia::ui::composition::ParentViewportStatus> status_helper_;
};

// An implementation of the ChildViewWatcher protocol, consisting of hanging gets for various
// updateable pieces of information.
class ChildViewWatcherImpl : public fuchsia::ui::composition::ChildViewWatcher {
 public:
  explicit ChildViewWatcherImpl(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                                fidl::InterfaceRequest<ChildViewWatcher> request,
                                LinkProtocolErrorCallback error_callback)
      : binding_(this, std::move(request), dispatcher_holder->dispatcher()),
        error_callback_(std::move(error_callback)),
        status_helper_(dispatcher_holder),
        viewref_helper_(std::move(dispatcher_holder)) {}

  void UpdateLinkStatus(fuchsia::ui::composition::ChildViewStatus status) {
    status_helper_.Update(std::move(status));
  }

  void SetViewRef(fuchsia::ui::views::ViewRef viewref) {
    viewref_helper_.Update(std::move(viewref));
  }

  // |fuchsia::ui::composition::ChildViewWatcher|
  void GetStatus(GetStatusCallback callback) override {
    if (status_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetStatus() called when there is a pending GetStatus() call. Flatland connection "
          "will be closed because of broken flow control.");
      return;
    }

    status_helper_.SetCallback(std::move(callback));
  }

  // |fuchsia::ui::composition::ChildViewWatcher|
  void GetViewRef(GetViewRefCallback callback) override {
    if (viewref_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetViewRef() called when there is a pending GetViewRef() call. Flatland connection "
          "will be closed because of broken flow control.");
      return;
    }

    viewref_helper_.SetCallback(std::move(callback));
  }

 private:
  fidl::Binding<fuchsia::ui::composition::ChildViewWatcher> binding_;
  LinkProtocolErrorCallback error_callback_;
  HangingGetHelper<fuchsia::ui::composition::ChildViewStatus> status_helper_;
  HangingGetHelper<fuchsia::ui::views::ViewRef> viewref_helper_;
};

// A system for managing links between Flatland instances. Each Flatland instance creates Links
// using tokens provided by Flatland clients. Each end of a Link consists of:
// - An implementation of the FIDL protocol for communicating with the other end of the link.
// - A TransformHandle which serves as the attachment point for the link.
// - The ObjectLinker link which serves as the actual implementation of the link.
//
// The LinkSystem is only responsible for connecting the "attachment point" TransformHandles
// returned in the Link structs. Flatland instances must attach these handles to their own
// transform hierarchy and notify the TopologySystem in order for the link to actually be
// established.
class LinkSystem : public std::enable_shared_from_this<LinkSystem> {
 public:
  explicit LinkSystem(TransformHandle::InstanceId instance_id);

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  LinkSystem(const LinkSystem&) = delete;
  LinkSystem& operator=(const LinkSystem&) = delete;
  LinkSystem(LinkSystem&&) = delete;
  LinkSystem& operator=(LinkSystem&&) = delete;

  // In addition to supplying an interface request via the ObjectLinker, the "child" end of a link
  // also supplies its attachment point so that the LinkSystem can create an edge between the two
  // when the link resolves. This allows creation and destruction logic to be paired within a single
  // ObjectLinker endpoint, instead of being spread out between the two endpoints.
  struct ChildLinkInfo {
    TransformHandle parent_viewport_watcher_handle;
    TransformHandle link_handle;
    fuchsia::math::SizeU initial_logical_size;
  };

  struct ParentLinkInfo {
    TransformHandle child_view_watcher_handle;
    std::shared_ptr<const fuchsia::ui::views::ViewRef> view_ref;
  };

  // Linked Flatland instances only implement a small piece of link functionality. For now, directly
  // sharing link requests is a clean way to implement that functionality. This will become more
  // complicated as the Flatland API evolves.
  using ObjectLinker = scenic_impl::gfx::ObjectLinker<ParentLinkInfo, ChildLinkInfo>;

  // Destruction of a ChildLink object will trigger deregistration with the LinkSystem.
  // Deregistration is thread safe, but the user of the Link object should be confident (e.g., by
  // tracking release fences) that no other systems will try to reference the Link.
  struct ChildLink {
    // The handle on which the ParentViewportWatcherImpl to the child will live.
    TransformHandle parent_viewport_watcher_handle;
    // The LinkSystem-owned handle that will be a key in the LinkTopologyMap when the link resolves.
    // These handles will never be in calculated global topologies; they are primarily used to
    // signal when to look for a link in GlobalTopologyData::ComputeGlobalTopologyData().
    TransformHandle link_handle;
    ObjectLinker::ImportLink importer;
  };

  // Destruction of a ParentLink object will trigger deregistration with the LinkSystem.
  // Deregistration is thread safe, but the user of the Link object should be confident (e.g., by
  // tracking release fences) that no other systems will try to reference the Link.
  struct ParentLink {
    // The handle that the ChildViewWatcherImpl to the parent will live on and will be a value in
    // the LinkTopologyMap when the link resolves.
    TransformHandle child_view_watcher_handle;
    ObjectLinker::ExportLink exporter;

    // Tracks the ViewRef for this View and is the reference for the lifetime of the ViewRef by
    // uniquely holding |view_ref_control| until going out of scope.
    std::shared_ptr<const fuchsia::ui::views::ViewRef> view_ref;
    fuchsia::ui::views::ViewRefControl view_ref_control;
  };

  // Creates the child end of a link. The ChildLink's |link_handle| serves as the attachment point
  // for the caller's transform hierarchy. |initial_properties| is immediately dispatched to the
  // ParentLink when the Link is resolved, regardless of whether the parent or the child has called
  // |Flatland::Present()|.
  //
  // Link handles are excluded from global topologies, so the |parent_viewport_watcher_handle| is
  // provided by the parent as the attachment point for the ChildViewWatcherImpl.
  //
  // |dispatcher_holder| allows hanging-get response-callbacks to be invoked from the appropriate
  // Flatland session thread.
  ChildLink CreateChildLink(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fuchsia::ui::views::ViewportCreationToken token,
      fuchsia::ui::composition::ViewportProperties initial_properties,
      fidl::InterfaceRequest<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher,
      TransformHandle parent_viewport_watcher_handle, LinkProtocolErrorCallback error_callback);

  // Creates the parent end of a link. Once both ends of a Link have been created, the LinkSystem
  // will create a local topology that connects the internal Link to the ParentLink's |link_origin|.
  //
  // |dispatcher_holder| allows hanging-get response-callbacks to be invoked from the appropriate
  // Flatland session thread.
  ParentLink CreateParentLink(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fuchsia::ui::views::ViewCreationToken token,
      fuchsia::ui::views::ViewIdentityOnCreation view_identity,
      fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
          parent_viewport_watcher,
      TransformHandle child_view_watcher_handle, LinkProtocolErrorCallback error_callback);

  // Returns a snapshot of the current set of links, represented as a map from LinkSystem-owned
  // TransformHandles to TransformHandles in ParentLinks. The LinkSystem generates Keys for this
  // map in CreateChildLink() and returns them to callers in a ChildLink's |link_handle|. The
  // values in this map are arguments to CreateParentLink() and become the ParentLink's
  // |link_origin|. The LinkSystem places entries in the map when a link resolves and removes them
  // when a link is invalidated.
  GlobalTopologyData::LinkTopologyMap GetResolvedTopologyLinks();

  // Returns the instance ID used for LinkSystem-authored handles.
  TransformHandle::InstanceId GetInstanceId() const;

  // For use by the core processing loop, this function consumes global information, processes it,
  // and sends all necessary updates to active ParentViewportWatcher and ChildViewWatcher channels.
  //
  // This data passed into this function is generated by merging information from multiple Flatland
  // instances. |global_topology| is the TopologyVector of all nodes visible from the (currently
  // single) display. |live_handles| is the set of nodes in that vector. |global_matrices| is the
  // list of global matrices, one per handle in |global_topology|. |uber_structs| is the set of
  // UberStructs used to generate the global topology.
  void UpdateLinks(const GlobalTopologyData::TopologyVector& global_topology,
                   const std::unordered_set<TransformHandle>& live_handles,
                   const GlobalMatrixVector& global_matrices, const glm::vec2& display_pixel_scale,
                   const UberStruct::InstanceMap& uber_structs);

 private:
  TransformHandle::InstanceId instance_id_;
  TransformGraph link_graph_;

  std::shared_ptr<ObjectLinker> linker_;

  // TODO(fxbug.dev/44335): These maps are modified at Link creation and destruction time (within
  // the ObjectLinker closures) as well as within UpdateLinks, which is called by the core render
  // loop. This produces a possible priority inversion between the Flatland instance threads and the
  // (possibly deadline scheduled) render thread.
  std::mutex map_mutex_;

  // A ParentViewportWatcherImpl and the |link_origin| of the child Flatland instance the impl
  // serves.
  struct ParentViewportWatcherData {
    std::shared_ptr<ParentViewportWatcherImpl> impl;
    TransformHandle child_link_origin;
  };
  std::unordered_map<TransformHandle, ParentViewportWatcherData> parent_viewport_watcher_map_;
  std::unordered_map<TransformHandle, std::shared_ptr<ChildViewWatcherImpl>>
      child_view_watcher_map_;
  // The set of current link topologies. Access is managed by |map_mutex_|.
  GlobalTopologyData::LinkTopologyMap link_topologies_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
