// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;

namespace flatland {

LinkSystem::LinkSystem(const std::shared_ptr<TopologySystem>& topology_system)
    : topology_system_(topology_system), link_graph_(topology_system_->CreateGraph()) {}

LinkSystem::ChildLink LinkSystem::CreateChildLink(
    ContentLinkToken token, fidl::InterfaceRequest<ContentLink> content_link) {
  auto impl = std::make_shared<GraphLinkImpl>();
  TransformHandle link_handle = link_graph_.CreateTransform();

  ObjectLinker::ImportLink importer =
      linker_.CreateImport(std::move(content_link), std::move(token.value),
                           /* error_reporter */ nullptr);

  importer.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl = impl, link_handle = link_handle](GraphLinkRequest request) {
        ref->graph_link_bindings_.AddBinding(impl, std::move(request.interface));
        ref->topology_system_->SetLocalTopology(
            {{link_handle, 0}, {request.remote_link_handle, 0}});
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl = impl, link_handle = link_handle](bool on_link_destruction) {
        ref->graph_link_bindings_.RemoveBinding(impl);
        ref->topology_system_->ClearLocalTopology(link_handle);
        ref->link_graph_.ReleaseTransform(link_handle);
      });

  return ChildLink({
      .impl = std::move(impl),
      .link_handle = link_handle,
      .importer = std::move(importer),
  });
}

LinkSystem::ParentLink LinkSystem::CreateParentLink(GraphLinkToken token,
                                                    fidl::InterfaceRequest<GraphLink> graph_link) {
  auto impl = std::make_shared<ContentLinkImpl>();
  TransformHandle link_handle = link_graph_.CreateTransform();

  ObjectLinker::ExportLink exporter =
      linker_.CreateExport({.interface = std::move(graph_link), .remote_link_handle = link_handle},
                           std::move(token.value), /* error_reporter */ nullptr);

  exporter.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl = impl](fidl::InterfaceRequest<ContentLink> request) {
        ref->content_link_bindings_.AddBinding(impl, std::move(request));
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl = impl, link_handle = link_handle](bool on_link_destruction) {
        ref->content_link_bindings_.RemoveBinding(impl);
        ref->link_graph_.ReleaseTransform(link_handle);
      });

  return ParentLink({
      .impl = std::move(impl),
      .link_handle = link_handle,
      .exporter = std::move(exporter),
  });
}

}  // namespace flatland
