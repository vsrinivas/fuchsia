// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/mat3x3.hpp>
#include <unordered_map>
#include <unordered_set>

#include "lib/fidl/cpp/interface_request.h"
#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"

namespace flatland {

// An implementation of the GraphLink protocol, consisting of hanging gets for various updateable
// pieces of information.
class GraphLinkImpl : public fuchsia::ui::scenic::internal::GraphLink {
 public:
  void UpdateLayoutInfo(fuchsia::ui::scenic::internal::LayoutInfo info) {
    layout_helper_.Update(std::move(info));
  }

  void UpdateLinkStatus(fuchsia::ui::scenic::internal::GraphLinkStatus status) {
    status_helper_.Update(std::move(status));
  }

  // |fuchsia::ui::scenic::internal::GraphLink|
  void GetLayout(GetLayoutCallback callback) override {
    // TODO(37750): Handle duplicate calls to a hanging get with an error, as the client is not
    // assuming the appropriate flow control.
    layout_helper_.SetCallback(
        [callback = std::move(callback)](fuchsia::ui::scenic::internal::LayoutInfo info) {
          callback(std::move(info));
        });
  }

  // |fuchsia::ui::scenic::internal::GraphLink|
  void GetStatus(GetStatusCallback callback) override {
    // TODO(37750): Handle duplicate calls to a hanging get with an error, as the client is not
    // assuming the appropriate flow control.
    status_helper_.SetCallback(std::move(callback));
  }

 private:
  HangingGetHelper<fuchsia::ui::scenic::internal::LayoutInfo> layout_helper_;
  HangingGetHelper<fuchsia::ui::scenic::internal::GraphLinkStatus> status_helper_;
};

// An implementation of the ContentLink protocol, consisting of hanging gets for various updateable
// pieces of information.
class ContentLinkImpl : public fuchsia::ui::scenic::internal::ContentLink {
 public:
  void UpdateLinkStatus(fuchsia::ui::scenic::internal::ContentLinkStatus status) {
    status_helper_.Update(std::move(status));
  }

  // |fuchsia::ui::scenic::internal::ContentLink|
  void GetStatus(GetStatusCallback callback) override {
    // TODO(37750): Handle duplicate calls to a hanging get with an error, as the client is not
    // assuming the appropriate flow control.
    status_helper_.SetCallback(std::move(callback));
  }

 private:
  HangingGetHelper<fuchsia::ui::scenic::internal::ContentLinkStatus> status_helper_;
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
  struct GraphLinkRequest {
    fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> interface;
    TransformHandle child_handle;
  };

  // Linked Flatland instances only implement a small piece of link functionality. For now, directly
  // sharing link requests is a clean way to implement that functionality. This will become more
  // complicated as the Flatland API evolves.
  using ObjectLinker = scenic_impl::gfx::ObjectLinker<
      GraphLinkRequest, fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink>>;

  // Destruction of a ChildLink object will trigger deregistration with the LinkSystem.
  // Deregistration is thread safe, but the user of the Link object should be confident (e.g., by
  // tracking release fences) that no other systems will try to reference the Link.
  struct ChildLink {
    // The handle on which the GraphLinkImpl to the child will live.
    TransformHandle graph_handle;
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
    // The handle that the ContentLinkImpl to the parent will live on and will be a value in the
    // LinkTopologyMap when the link resolves.
    TransformHandle link_origin;
    ObjectLinker::ExportLink exporter;
  };

  // Creates the child end of a link. The ChildLink's |link_handle| serves as the attachment point
  // for the caller's transform hierarchy. |initial_properties| is immediately dispatched to the
  // ParentLink when the Link is resolved, regardless of whether the parent or the child has called
  // |Flatland::Present()|.
  //
  // Link handles are excluded from global topologies, so the |graph_handle| is provided by the
  // parent as the attachment point for the ContentLinkImpl.
  ChildLink CreateChildLink(
      fuchsia::ui::scenic::internal::ContentLinkToken token,
      fuchsia::ui::scenic::internal::LinkProperties initial_properties,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink> content_link,
      TransformHandle graph_handle);

  // Creates the parent end of a link. Once both ends of a Link have been created, the LinkSystem
  // will create a local topology that connects the internal Link to the ParentLink's |link_origin|.
  ParentLink CreateParentLink(
      fuchsia::ui::scenic::internal::GraphLinkToken token,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> graph_link,
      TransformHandle link_origin);

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
  // and sends all necessary updates to active GraphLink and ContentLink channels.
  //
  // This data passed into this function is generated by merging information from multiple Flatland
  // instances. |global_topology| is the ToplogyVector of all nodes visible from the (currently
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

  ObjectLinker linker_;

  // TODO(44335): These maps are modified at Link creation and destruction time (within the
  // ObjectLinker closures) as well as within UpdateLinks, which is called by the core render loop.
  // This produces a possible priority inversion between the Flatland instance threads and the
  // (possibly deadline scheduled) render thread.
  std::mutex map_mutex_;

  // A GraphLinkImpl and the |link_origin| of the child Flatland instance the impl serves.
  struct GraphLinkData {
    std::shared_ptr<GraphLinkImpl> impl;
    TransformHandle child_link_origin;
  };
  std::unordered_map<TransformHandle, GraphLinkData> graph_link_map_;
  std::unordered_map<TransformHandle, std::shared_ptr<ContentLinkImpl>> content_link_map_;
  // The set of current link topologies. Access is managed by |map_mutex_|.
  GlobalTopologyData::LinkTopologyMap link_topologies_;

  // Any FIDL requests that have to be bound, are bound in these BindingSets. All impl classes are
  // referenced by both these sets and the Flatland instance that created them via creation of a
  // link. Entries in these sets are controlled entirely by the link resolution and failure
  // callbacks that exist in the ObjectLinker links.
  fidl::BindingSet<fuchsia::ui::scenic::internal::GraphLink, std::shared_ptr<GraphLinkImpl>>
      graph_link_bindings_;
  fidl::BindingSet<fuchsia::ui::scenic::internal::ContentLink, std::shared_ptr<ContentLinkImpl>>
      content_link_bindings_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
