// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_LINK_SYSTEM_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "lib/fidl/cpp/interface_request.h"
#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"
#include "src/ui/scenic/lib/flatland/topology_system.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"

namespace flatland {

// An implementation of the GraphLink protocol, consisting of hanging gets for various updateable
// pieces of information.
class GraphLinkImpl : public fuchsia::ui::scenic::internal::GraphLink {
 public:
  void UpdateLayoutInfo(fuchsia::ui::scenic::internal::LayoutInfo info) {
    layout_helper_.Update(std::move(info));
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

 private:
  HangingGetHelper<fuchsia::ui::scenic::internal::LayoutInfo> layout_helper_;
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
  explicit LinkSystem(const std::shared_ptr<TopologySystem>& topology_system);

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  LinkSystem(const LinkSystem&) = delete;
  LinkSystem& operator=(const LinkSystem&) = delete;
  LinkSystem(LinkSystem&&) = delete;
  LinkSystem& operator=(LinkSystem&&) = delete;

  // In addition to supplying an interface request via the ObjectLinker, the "child" end of a link
  // also supplies its attachment point so that the LinkSystem can create an edge between the two
  // when the link resolves.
  struct GraphLinkRequest {
    fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> interface;
    TransformHandle remote_link_handle;
  };

  // Linked Flatland instances only implement a small piece of link functionality. For now, directly
  // sharing link requests is a clean way to implement that functionality. This will become more
  // complicated as the Flatland API evolves.
  using ObjectLinker = scenic_impl::gfx::ObjectLinker<
      GraphLinkRequest, fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink>>;

  // This is a strong reference to the GraphLink in the child Flatland instance. This makes it easy
  // for methods called on the ContentLink (e.g., SetLinkProperties()) to be transformed into output
  // events on the child's channel.
  struct ChildLink {
    std::shared_ptr<GraphLinkImpl> impl;
    TransformHandle link_handle;
    ObjectLinker::ImportLink importer;
  };

  // This is a strong reference to the ContentLinkImpl in the parent Flatland instance. This makes
  // it easy for methods that effect the ContentLink (e.g., Present()) to be transformed into output
  // events on the parent's channel.
  struct ParentLink {
    std::shared_ptr<ContentLinkImpl> impl;
    TransformHandle link_handle;
    ObjectLinker::ExportLink exporter;
  };

  // Creates the child end of a link, including an already-initialized GraphLinkImpl so that the
  // caller can begin queueing data for the child immediately. The ChildLink's |link_handle| serves
  // as the attachment point for the caller's transform hierarchy.
  ChildLink CreateChildLink(
      fuchsia::ui::scenic::internal::ContentLinkToken token,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink> content_link);

  // Creates the parent end of a link, including an already-initialized ContentLinkImpl so that the
  // caller can begin queueing data for the parent immediately. The ParentLink's |link_handle|
  // serves as the attachment point for the caller's transform hierarchy.
  ParentLink CreateParentLink(
      fuchsia::ui::scenic::internal::GraphLinkToken token,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> graph_link);

 private:
  // The LinkSystem updates the local topology of attachment point link handles. All link handles
  // are allocated from a separate TransformGraph dedicated to links to centralize cleanup of said
  // handles.
  std::shared_ptr<TopologySystem> topology_system_;
  TransformGraph link_graph_;

  ObjectLinker linker_;

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
