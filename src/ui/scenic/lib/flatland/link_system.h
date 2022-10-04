// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_

#include <fidl/fuchsia.ui.composition/cpp/fidl.h>
#include <fidl/fuchsia.ui.composition/cpp/hlcpp_conversion.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/errors.h>

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

#include <glm/glm.hpp>
#include <glm/mat3x3.hpp>

namespace flatland {

// Used by to communicate back to `LinkSystem` callers that a `ParentViewportWatcher` or
// `ChildViewWatcher` client performed an illegal action.  For example, this is used by Flatland
// to close down the associated Flatland session with an error.
using LinkProtocolErrorCallback = std::function<void(const std::string&)>;

// An implementation of the ParentViewportWatcher protocol, consisting of hanging gets for various
// updateable pieces of information.
class ParentViewportWatcherImpl
    : public fidl::Server<fuchsia_ui_composition::ParentViewportWatcher> {
 public:
  ParentViewportWatcherImpl(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher> request,
      LinkProtocolErrorCallback error_callback)
      : binding_ref_(fidl::BindServer(
            dispatcher_holder->dispatcher(), fidl::HLCPPToNatural(request), this,
            [](ParentViewportWatcherImpl* /*unused*/, fidl::UnbindInfo info,
               fidl::ServerEnd<fuchsia_ui_composition::ParentViewportWatcher> /*unused*/) {
              OnUnbound(info);
            })),
        error_callback_(std::move(error_callback)) {}

  ~ParentViewportWatcherImpl() override {
    // `ServerBindingRef` doesn't have RAII semantics for destroying the underlying channel, so it
    // must be done explicitly to avoid "leaking" the channel (not forever, rather for the lifetime
    // of the dispatcher, i.e. the lifetime of the associated View's Flatland session).
    binding_ref_.Unbind();
  }

  void UpdateLayoutInfo(fuchsia::ui::composition::LayoutInfo info) {
    layout_helper_.Update(std::move(info));
  }

  void UpdateLinkStatus(fuchsia::ui::composition::ParentViewportStatus status) {
    status_helper_.Update(std::move(status));
  }
  // |fuchsia::ui::composition::ParentViewportWatcher|
  void GetLayout(GetLayoutRequest& request, GetLayoutCompleter::Sync& sync_completer) override {
    if (layout_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetLayout() called when there is a pending GetLayout() call. Flatland connection "
          "will be closed because of broken flow control.");
      sync_completer.Close(ZX_ERR_SHOULD_WAIT);
      return;
    }

    layout_helper_.SetCallback([completer = sync_completer.ToAsync()](
                                   fuchsia::ui::composition::LayoutInfo layout_info) mutable {
      completer.Reply({fidl::HLCPPToNatural(layout_info)});
    });
  }

  // |fuchsia_ui_composition::ParentViewportWatcher|
  void GetStatus(GetStatusRequest& request, GetStatusCompleter::Sync& sync_completer) override {
    if (status_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetStatus() called when there is a pending GetStatus() call. Flatland connection "
          "will be closed because of broken flow control.");
      sync_completer.Close(ZX_ERR_SHOULD_WAIT);
      return;
    }

    status_helper_.SetCallback([completer = sync_completer.ToAsync()](
                                   fuchsia::ui::composition::ParentViewportStatus status) mutable {
      completer.Reply({fidl::HLCPPToNatural(status)});
    });
  }

 private:
  // Called when the connection is torn down, shortly before the implementation is destroyed.
  static void OnUnbound(fidl::UnbindInfo info) {
    if (info.is_peer_closed()) {
      FX_LOGS(DEBUG) << "ParentViewportWatcherImpl::OnUnbound()  Client disconnected";
    } else if (!info.is_user_initiated()) {
      FX_LOGS(WARNING) << "ParentViewportWatcherImpl::OnUnbound()  server error: " << info;
    }
  }

  fidl::ServerBindingRef<fuchsia_ui_composition::ParentViewportWatcher> binding_ref_;
  LinkProtocolErrorCallback error_callback_;
  HangingGetHelper<fuchsia::ui::composition::LayoutInfo> layout_helper_;
  HangingGetHelper<fuchsia::ui::composition::ParentViewportStatus> status_helper_;
};

// An implementation of the ChildViewWatcher protocol, consisting of hanging gets for various
// updateable pieces of information.
class ChildViewWatcherImpl : public fidl::Server<fuchsia_ui_composition::ChildViewWatcher> {
 public:
  ChildViewWatcherImpl(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                       fidl::InterfaceRequest<fuchsia::ui::composition::ChildViewWatcher> request,
                       LinkProtocolErrorCallback error_callback)
      : binding_ref_(fidl::BindServer(
            dispatcher_holder->dispatcher(), fidl::HLCPPToNatural(request), this,
            [](ChildViewWatcherImpl* /*unused*/, fidl::UnbindInfo info,
               fidl::ServerEnd<fuchsia_ui_composition::ChildViewWatcher> /*unused*/) {
              OnUnbound(info);
            })),
        error_callback_(std::move(error_callback)) {}

  ~ChildViewWatcherImpl() override {
    // `ServerBindingRef` doesn't have RAII semantics for destroying the underlying channel, so it
    // must be done explicitly to avoid "leaking" the channel (not forever, rather for the lifetime
    // of the dispatcher, i.e. the lifetime of the associated Viewport's Flatland session).
    binding_ref_.Unbind();
  }

  void UpdateLinkStatus(fuchsia::ui::composition::ChildViewStatus status) {
    status_helper_.Update(status);
    if (viewref_) {
      // At the time of writing, CONTENT_HAS_PRESENTED is the only possible value.  DCHECK just in
      // case this changes.
      FX_DCHECK(status == fuchsia::ui::composition::ChildViewStatus::CONTENT_HAS_PRESENTED);
    }
  }

  // If ViewRef hasn't yet been pushed to the hanging get helper, do so.
  void UpdateViewRef() {
    if (viewref_) {
      viewref_helper_.Update(std::move(viewref_.value()));
      viewref_.reset();
    }
  }

  void SetViewRef(fuchsia::ui::views::ViewRef viewref) {
    FX_CHECK(viewref.reference);
    viewref_ = std::move(viewref);
  }

  // |fuchsia_ui_composition::ChildViewWatcher|
  void GetStatus(GetStatusRequest& request, GetStatusCompleter::Sync& sync_completer) override {
    if (status_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetStatus() called when there is a pending GetStatus() call. Flatland connection "
          "will be closed because of broken flow control.");
      sync_completer.Close(ZX_ERR_SHOULD_WAIT);
      return;
    }

    status_helper_.SetCallback([completer = sync_completer.ToAsync()](
                                   fuchsia::ui::composition::ChildViewStatus status) mutable {
      completer.Reply({fidl::HLCPPToNatural(status)});
    });
  }

  // |fuchsia_ui_composition::ChildViewWatcher|
  void GetViewRef(GetViewRefRequest& request, GetViewRefCompleter::Sync& sync_completer) override {
    if (viewref_helper_.HasPendingCallback()) {
      FX_DCHECK(error_callback_);
      error_callback_(
          "GetViewRef() called when there is a pending GetViewRef() call. Flatland connection "
          "will be closed because of broken flow control.");
      sync_completer.Close(ZX_ERR_SHOULD_WAIT);
      return;
    }

    viewref_helper_.SetCallback(
        [completer = sync_completer.ToAsync()](fuchsia::ui::views::ViewRef viewref) mutable {
          completer.Reply({fidl::HLCPPToNatural(viewref)});
        });
  }

 private:
  // Called when the connection is torn down, shortly before the implementation is destroyed.
  static void OnUnbound(fidl::UnbindInfo info) {
    if (info.is_peer_closed()) {
      FX_LOGS(DEBUG) << "ChildViewWatcherImpl::OnUnbound()  Client disconnected";
    } else if (!info.is_user_initiated()) {
      FX_LOGS(WARNING) << "ChildViewWatcherImpl::OnUnbound()  server error: " << info;
    }
  }

  fidl::ServerBindingRef<fuchsia_ui_composition::ChildViewWatcher> binding_ref_;
  LinkProtocolErrorCallback error_callback_;
  HangingGetHelper<fuchsia::ui::composition::ChildViewStatus> status_helper_;
  HangingGetHelper<fuchsia::ui::views::ViewRef> viewref_helper_;

  // Temporarily held when SetViewRef() is called.  Instead of immediately notifying any pending
  // hanging get requests, we wait until the child view first appears in the global topology.
  std::optional<fuchsia::ui::views::ViewRef> viewref_;
};

// A system for managing links between Flatland instances. Each Flatland instance creates Links
// using tokens provided by Flatland clients. Each end of a Link consists of:
// - An implementation of the FIDL protocol for communicating with the other end of the link.
// - A TransformHandle which serves as the "attachment point" for that end of the link.
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

  // In addition to supplying an interface request via the ObjectLinker, the "ToChild" end of a link
  // also supplies its attachment point so that the LinkSystem can create an edge between the two
  // when the link resolves. This allows creation and destruction logic to be paired within a single
  // ObjectLinker endpoint, instead of being spread out between the two endpoints.
  struct LinkToChildInfo {
    TransformHandle parent_transform_handle;
    TransformHandle internal_link_handle;
    fuchsia::math::SizeU initial_logical_size;
    fuchsia::math::Inset initial_inset;
  };

  struct LinkToParentInfo {
    TransformHandle child_transform_handle;
    std::shared_ptr<const fuchsia::ui::views::ViewRef> view_ref;
  };

  // Linked Flatland instances only implement a small piece of link functionality. For now, directly
  // sharing link requests is a clean way to implement that functionality. This will become more
  // complicated as the Flatland API evolves.
  using ObjectLinker = scenic_impl::gfx::ObjectLinker<LinkToParentInfo, LinkToChildInfo>;

  // Destruction of a LinkToChild object will trigger deregistration with the LinkSystem.
  // Deregistration is thread safe, but the user of the Link object should be confident (e.g., by
  // tracking release fences) that no other systems will try to reference the Link.
  struct LinkToChild {
    // The handle on which the ParentViewportWatcherImpl will live.
    TransformHandle parent_transform_handle;
    // The LinkSystem-owned handle that will be a key in the LinkTopologyMap when the link resolves.
    // These handles will never be in calculated global topologies; they are primarily used to
    // signal when to look for a link in GlobalTopologyData::ComputeGlobalTopologyData().
    TransformHandle internal_link_handle;
    ObjectLinker::ImportLink importer;
  };

  // Destruction of a LinkToParent object will trigger deregistration with the LinkSystem.
  // Deregistration is thread safe, but the user of the Link object should be confident (e.g., by
  // tracking release fences) that no other systems will try to reference the Link.
  struct LinkToParent {
    // The handle that the ChildViewWatcherImpl will live on and will be a value in the
    // LinkTopologyMap when the link resolves.
    TransformHandle child_transform_handle;
    ObjectLinker::ExportLink exporter;

    // Tracks the ViewRef for this View and is the reference for the lifetime of the ViewRef by
    // uniquely holding |view_ref_control| until going out of scope.
    std::shared_ptr<const fuchsia::ui::views::ViewRef> view_ref;

    // |view_ref_control| and |view_ref| are set when there is a valid ViewIdentityOnCreation.
    // Otherwise both are kept empty.
    std::optional<fuchsia::ui::views::ViewRefControl> view_ref_control;
  };

  // Creates the parent end of a link. The LinkToChild's |internal_link_handle| serves as the
  // attachment point for the caller's transform hierarchy. |initial_properties| is immediately
  // dispatched to the LinkToParent when the Link is resolved, regardless of whether the parent or
  // the child has called |Flatland::Present()|.
  //
  // Link handles are excluded from global topologies, so the |parent_transform_handle| is
  // provided by the parent as the attachment point for the ChildViewWatcherImpl.
  //
  // |dispatcher_holder| allows hanging-get response-callbacks to be invoked from the appropriate
  // Flatland session thread.
  LinkToChild CreateLinkToChild(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fuchsia::ui::views::ViewportCreationToken token,
      fuchsia::ui::composition::ViewportProperties initial_properties,
      fidl::InterfaceRequest<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher,
      TransformHandle parent_transform_handle, LinkProtocolErrorCallback error_callback);

  // Creates the child end of a link. Once both ends of a Link have been created, the LinkSystem
  // will create a local topology that connects the internal Link to the LinkToParent's
  // |child_transform_handle|.
  //
  // |dispatcher_holder| allows hanging-get response-callbacks to be invoked from the appropriate
  // Flatland session thread.
  LinkToParent CreateLinkToParent(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fuchsia::ui::views::ViewCreationToken token,
      std::optional<fuchsia::ui::views::ViewIdentityOnCreation> view_identity,
      fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
          parent_viewport_watcher,
      TransformHandle child_transform_handle, LinkProtocolErrorCallback error_callback);

  // Returns a snapshot of the current set of links, represented as a map from LinkSystem-owned
  // TransformHandles to TransformHandles in LinkToParents. The LinkSystem generates Keys for this
  // map in CreateLinkToChild() and returns them to callers in a LinkToChild's
  // |internal_link_handle|. The values in this map are arguments to CreateLinkToParent() and become
  // the LinkToParent's |child_transform_handle|. The LinkSystem places entries in the map when a
  // link resolves and removes them when a link is invalidated.
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
                   const GlobalMatrixVector& global_matrices, const glm::vec2& device_pixel_ratio,
                   const UberStruct::InstanceMap& uber_structs);

  // Returns the mapping from the child_transform_handle of each LinkToParent to the corresponding
  // parent_transform_handle from each LinkToChild.
  std::unordered_map<TransformHandle, TransformHandle> const GetLinkChildToParentTransformMap();

  void set_initial_device_pixel_ratio(const glm::vec2& initial_device_pixel_ratio) {
    initial_device_pixel_ratio_ = initial_device_pixel_ratio;
  }

 private:
  TransformHandle CreateTransformLocked() {
    TransformHandle transform;
    {
      std::scoped_lock lock(mutex_);
      transform = link_graph_.CreateTransform();
    }
    return transform;
  }

  TransformHandle::InstanceId instance_id_;

  // |link_graph_|, an instance of a TransformGraph, is not thread safe, as it is designed to be
  // used by individual Flatland instances. However, this class is shared across all Flatland
  // instances, and therefore different threads. Therefore, access to |link_graph_| should be
  // guarded by |mutex_|.
  TransformGraph link_graph_ TA_GUARDED(mutex_);

  std::shared_ptr<ObjectLinker> linker_;

  // |mutex_| guards access to |link_graph_| and |link_topologies_|.
  //
  // TODO(fxbug.dev/44335): These maps are modified at Link creation and destruction time (within
  // the ObjectLinker closures) as well as within UpdateLinks, which is called by the core render
  // loop. This produces a possible priority inversion between the Flatland instance threads and the
  // (possibly deadline scheduled) render thread.
  std::mutex mutex_;

  // Structs representing the child and parent ends of a link.
  struct ChildEnd {
    std::shared_ptr<ParentViewportWatcherImpl> parent_viewport_watcher;
    TransformHandle child_transform_handle;
  };
  struct ParentEnd {
    std::shared_ptr<ChildViewWatcherImpl> child_view_watcher;
  };

  // Keyed by LinkToChild::parent_transform_handle.
  std::unordered_map<TransformHandle, ChildEnd> parent_to_child_map_;
  // Keyed by LinkToParent::child_transform_handle.
  std::unordered_map<TransformHandle, ParentEnd> child_to_parent_map_;
  // The set of current link topologies. Access is managed by |mutex_|.
  GlobalTopologyData::LinkTopologyMap link_topologies_ TA_GUARDED(mutex_);

  // The starting DPR used by the link system. The actual DPR used on subsequent calls to
  // UpdateLinks() may be different from this value.
  // TODO(fxbug.dev/108608): This will need to be updated once we have multidisplay setup.
  glm::vec2 initial_device_pixel_ratio_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
