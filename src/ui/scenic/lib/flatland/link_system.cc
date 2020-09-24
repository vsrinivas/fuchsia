// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

#include <lib/syslog/cpp/macros.h>

#include <glm/gtc/matrix_access.hpp>

using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkStatus;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;

namespace flatland {

namespace {

glm::vec2 ComputeScale(const glm::mat3& matrix) {
  const glm::vec3 x_row = glm::row(matrix, 0);
  const glm::vec3 y_row = glm::row(matrix, 1);
  return {std::fabs(glm::length(x_row)), std::fabs(glm::length(y_row))};
}

}  // namespace

LinkSystem::LinkSystem(TransformHandle::InstanceId instance_id)
    : instance_id_(instance_id), link_graph_(instance_id_) {}

LinkSystem::ChildLink LinkSystem::CreateChildLink(
    ContentLinkToken token, fuchsia::ui::scenic::internal::LinkProperties initial_properties,
    fidl::InterfaceRequest<ContentLink> content_link, TransformHandle graph_handle) {
  FX_DCHECK(token.value.is_valid());

  auto impl = std::make_shared<GraphLinkImpl>();
  const TransformHandle link_handle = link_graph_.CreateTransform();

  ObjectLinker::ImportLink importer =
      linker_.CreateImport(std::move(content_link), std::move(token.value),
                           /* error_reporter */ nullptr);

  importer.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl = impl, graph_handle = graph_handle,
       link_handle = link_handle,
       initial_properties = std::move(initial_properties)](GraphLinkRequest request) {
        // Immediately send out the initial properties over the channel. This callback is fired from
        // one of the Flatland instance threads, but since we haven't stored the Link impl anywhere
        // yet, we still have exclusive access and can safely call functions without
        // synchronization.
        if (initial_properties.has_logical_size()) {
          LayoutInfo info;
          info.set_logical_size(initial_properties.logical_size());
          impl->UpdateLayoutInfo(std::move(info));
        }

        {
          // Mutate shared state while holding our mutex.
          std::scoped_lock lock(ref->map_mutex_);
          ref->graph_link_bindings_.AddBinding(impl, std::move(request.interface));
          ref->graph_link_map_[graph_handle] =
              GraphLinkData({.impl = impl, .child_link_origin = request.child_handle});

          // The topology is constructed here, instead of in the link_resolved closure of the
          // ParentLink object, so that its destruction (which depends on the link_handle) can occur
          // on the same endpoint.
          ref->link_topologies_[link_handle] = request.child_handle;
        }
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl = impl, graph_handle = graph_handle,
       link_handle = link_handle](bool on_link_destruction) {
        {
          std::scoped_lock lock(ref->map_mutex_);
          ref->graph_link_map_.erase(graph_handle);
          ref->graph_link_bindings_.RemoveBinding(impl);

          ref->link_topologies_.erase(link_handle);
          ref->link_graph_.ReleaseTransform(link_handle);
        }
      });

  return ChildLink({
      .graph_handle = graph_handle,
      .link_handle = link_handle,
      .importer = std::move(importer),
  });
}

LinkSystem::ParentLink LinkSystem::CreateParentLink(GraphLinkToken token,
                                                    fidl::InterfaceRequest<GraphLink> graph_link,
                                                    TransformHandle link_origin) {
  FX_DCHECK(token.value.is_valid());

  auto impl = std::make_shared<ContentLinkImpl>();

  ObjectLinker::ExportLink exporter =
      linker_.CreateExport({.interface = std::move(graph_link), .child_handle = link_origin},
                           std::move(token.value), /* error_reporter */ nullptr);

  exporter.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl = impl,
       link_origin = link_origin](fidl::InterfaceRequest<ContentLink> request) {
        std::scoped_lock lock(ref->map_mutex_);
        ref->content_link_bindings_.AddBinding(impl, std::move(request));
        ref->content_link_map_[link_origin] = impl;
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl = impl, link_origin = link_origin](bool on_link_destruction) {
        std::scoped_lock lock(ref->map_mutex_);
        ref->content_link_map_.erase(link_origin);
        ref->content_link_bindings_.RemoveBinding(impl);
      });

  return ParentLink({
      .link_origin = link_origin,
      .exporter = std::move(exporter),
  });
}

void LinkSystem::UpdateLinks(const GlobalTopologyData::TopologyVector& global_topology,
                             const std::unordered_set<TransformHandle>& live_handles,
                             const GlobalMatrixVector& global_matrices,
                             const glm::vec2& display_pixel_scale,
                             const UberStruct::InstanceMap& uber_structs) {
  std::scoped_lock lock(map_mutex_);

  // Since the global topology may not contain every Flatland instance, manually update the
  // GraphLinkStatus of every GraphLink.
  for (auto& [graph_handle, graph_link] : graph_link_map_) {
    // The child Flatland instance is connected to the display if it is present in the global
    // topology.
    if (!live_handles.count(graph_link.child_link_origin)) {
      graph_link.impl->UpdateLinkStatus(GraphLinkStatus::DISCONNECTED_FROM_DISPLAY);
    } else {
      graph_link.impl->UpdateLinkStatus(GraphLinkStatus::CONNECTED_TO_DISPLAY);
    }
  }

  // The ContentLinkStatus changes the first time the child presents with a particular parent link.
  // This is indicated by an UberStruct with the |link_origin| as its first TransformHandle in the
  // snapshot. The LinkSystem can technically "miss" updating the ContentLinkStatus for a
  // particular ContentLink if the child presents two LinkToParent() calls before UpdateLinks() is
  // called, but in that case, the first Link is destroyed, and therefore its status does not need
  // to be updated anyway.
  for (auto& [link_origin, content_link] : content_link_map_) {
    auto uber_struct_kv = uber_structs.find(link_origin.GetInstanceId());
    if (uber_struct_kv != uber_structs.end()) {
      const auto& local_topology = uber_struct_kv->second->local_topology;

      // If the local topology doesn't start with the |link_origin|, the child is linked to a
      // different parent now, but the link_invalidated callback to remove this entry has not fired
      // yet.
      if (!local_topology.empty() && local_topology.front().handle == link_origin) {
        content_link->UpdateLinkStatus(ContentLinkStatus::CONTENT_HAS_PRESENTED);
      }
    }
  }

  for (size_t i = 0; i < global_topology.size(); ++i) {
    const auto& handle = global_topology[i];

    // For a particular Link, the LinkProperties and GraphLinkImpl both live on the ChildLink's
    // |graph_handle|. They can show up in either order (LinkProperties before GraphLinkImpl if the
    // parent Flatland calls Present() first, other way around if the link resolves first), so one
    // being present without another is not a bug.
    auto graph_kv = graph_link_map_.find(handle);
    if (graph_kv != graph_link_map_.end()) {
      auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
      if (uber_struct_kv != uber_structs.end()) {
        auto properties_kv = uber_struct_kv->second->link_properties.find(handle);
        if (properties_kv != uber_struct_kv->second->link_properties.end() &&
            properties_kv->second.has_logical_size()) {
          const auto pixel_scale = display_pixel_scale * ComputeScale(global_matrices[i]);
          LayoutInfo info;
          info.set_logical_size(properties_kv->second.logical_size());
          info.set_pixel_scale({pixel_scale.x, pixel_scale.y});
          graph_kv->second.impl->UpdateLayoutInfo(std::move(info));
        }
      }
    }
  }
}

GlobalTopologyData::LinkTopologyMap LinkSystem::GetResolvedTopologyLinks() {
  GlobalTopologyData::LinkTopologyMap copy;

  // Acquire the lock and copy.
  {
    std::scoped_lock lock(map_mutex_);
    copy = link_topologies_;
  }
  return copy;
}

TransformHandle::InstanceId LinkSystem::GetInstanceId() const { return instance_id_; }

}  // namespace flatland
