// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

#include <lib/syslog/cpp/macros.h>

#include <glm/gtc/matrix_access.hpp>

using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportStatus;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace flatland {

namespace {

glm::vec2 ComputeScale(const glm::mat3& matrix) {
  const glm::vec3 x_row = glm::row(matrix, 0);
  const glm::vec3 y_row = glm::row(matrix, 1);
  return {std::fabs(glm::length(x_row)), std::fabs(glm::length(y_row))};
}

}  // namespace

LinkSystem::LinkSystem(TransformHandle::InstanceId instance_id)
    : instance_id_(instance_id), link_graph_(instance_id_), linker_(ObjectLinker::New()) {}

LinkSystem::ChildLink LinkSystem::CreateChildLink(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder, ViewportCreationToken token,
    fuchsia::ui::composition::ViewportProperties initial_properties,
    fidl::InterfaceRequest<ChildViewWatcher> child_view_watcher,
    TransformHandle parent_viewport_watcher_handle, LinkProtocolErrorCallback error_callback) {
  FX_DCHECK(token.value.is_valid());
  FX_DCHECK(initial_properties.has_logical_size());

  auto impl = std::make_shared<ChildViewWatcherImpl>(dispatcher_holder,
                                                     std::move(child_view_watcher), error_callback);
  const TransformHandle link_handle = link_graph_.CreateTransform();

  ObjectLinker::ImportLink importer =
      linker_->CreateImport({.parent_viewport_watcher_handle = parent_viewport_watcher_handle,
                             .link_handle = link_handle,
                             .initial_logical_size = initial_properties.logical_size()},
                            std::move(token.value),
                            /* error_reporter */ nullptr);

  auto child_view_watcher_map_key = std::make_shared<TransformHandle>();
  importer.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl, child_view_watcher_map_key](ParentLinkInfo info) mutable {
        *child_view_watcher_map_key = info.child_view_watcher_handle;

        std::scoped_lock lock(ref->map_mutex_);
        ref->child_view_watcher_map_[*child_view_watcher_map_key] = impl;
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl, child_view_watcher_map_key](bool on_link_destruction) {
        // We expect |child_view_watcher_map_key| to be assigned by the "link_resolved" closure, but
        // this might not happen if the link is being destroyed before it was resolved.
        FX_DCHECK(child_view_watcher_map_key || on_link_destruction);
        std::scoped_lock lock(ref->map_mutex_);
        ref->child_view_watcher_map_.erase(*child_view_watcher_map_key);
      },
      dispatcher_holder);

  return ChildLink({
      .parent_viewport_watcher_handle = parent_viewport_watcher_handle,
      .link_handle = link_handle,
      .importer = std::move(importer),
  });
}

LinkSystem::ParentLink LinkSystem::CreateParentLink(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder, ViewCreationToken token,
    fidl::InterfaceRequest<ParentViewportWatcher> parent_viewport_watcher,
    TransformHandle child_view_watcher_handle, LinkProtocolErrorCallback error_callback) {
  FX_DCHECK(token.value.is_valid());

  auto impl = std::make_shared<ParentViewportWatcherImpl>(
      dispatcher_holder, std::move(parent_viewport_watcher), error_callback);

  ObjectLinker::ExportLink exporter = linker_->CreateExport(
      {.child_view_watcher_handle = child_view_watcher_handle}, std::move(token.value),
      /* error_reporter */ nullptr);

  auto parent_viewport_watcher_map_key = std::make_shared<TransformHandle>();
  auto topology_map_key = std::make_shared<TransformHandle>();
  exporter.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl, parent_viewport_watcher_map_key, topology_map_key,
       child_view_watcher_handle](ChildLinkInfo info) {
        *parent_viewport_watcher_map_key = info.parent_viewport_watcher_handle;
        *topology_map_key = info.link_handle;

        std::scoped_lock lock(ref->map_mutex_);
        // TODO(fxbug.dev/80603): When the same parent relinks to different children, we might be
        // using an outdated logical_size here. It will be corrected in UpdateLinks(), but we should
        // figure out a way to set the previous ParentViewportWatcherImpl's size here.
        LayoutInfo layout_info;
        layout_info.set_logical_size(info.initial_logical_size);
        layout_info.set_pixel_scale({1, 1});
        impl->UpdateLayoutInfo(std::move(layout_info));

        ref->parent_viewport_watcher_map_[*parent_viewport_watcher_map_key] =
            ParentViewportWatcherData(
                {.impl = impl, .child_link_origin = child_view_watcher_handle});
        // The topology is constructed here, instead of in the link_resolved closure of the
        // ParentLink object, so that its destruction (which depends on the link_handle) can occur
        // on the same endpoint.
        ref->link_topologies_[*topology_map_key] = child_view_watcher_handle;
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl, parent_viewport_watcher_map_key,
       topology_map_key](bool on_link_destruction) {
        // We expect |parent_viewport_watcher_map_key| and |topology_map_key| to be assigned by the
        // "link_resolved" closure, but this might not happen if the link is being destroyed before
        // it was resolved.
        FX_DCHECK((parent_viewport_watcher_map_key && topology_map_key) || on_link_destruction);
        std::scoped_lock lock(ref->map_mutex_);
        ref->parent_viewport_watcher_map_.erase(*parent_viewport_watcher_map_key);

        ref->link_topologies_.erase(*topology_map_key);
        ref->link_graph_.ReleaseTransform(*topology_map_key);
      },
      dispatcher_holder);

  return ParentLink({
      .child_view_watcher_handle = child_view_watcher_handle,
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
  // ParentViewportStatus of every ParentViewportWatcher.
  for (auto& [graph_handle, parent_viewport_watcher] : parent_viewport_watcher_map_) {
    // The child Flatland instance is connected to the display if it is present in the global
    // topology.
    if (!live_handles.count(parent_viewport_watcher.child_link_origin)) {
      parent_viewport_watcher.impl->UpdateLinkStatus(
          ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);
    } else {
      parent_viewport_watcher.impl->UpdateLinkStatus(ParentViewportStatus::CONNECTED_TO_DISPLAY);
    }
  }

  // The ChildViewStatus changes the first time the child presents with a particular parent link.
  // This is indicated by an UberStruct with the |link_origin| as its first TransformHandle in the
  // snapshot. The LinkSystem can technically "miss" updating the ChildViewStatus for a
  // particular ChildViewWatcher if the child presents two CreateView() calls before UpdateLinks()
  // is called, but in that case, the first Link is destroyed, and therefore its status does not
  // need to be updated anyway.
  for (auto& [link_origin, child_view_watcher] : child_view_watcher_map_) {
    auto uber_struct_kv = uber_structs.find(link_origin.GetInstanceId());
    if (uber_struct_kv != uber_structs.end()) {
      const auto& local_topology = uber_struct_kv->second->local_topology;

      // If the local topology doesn't start with the |link_origin|, the child is linked to a
      // different parent now, but the link_invalidated callback to remove this entry has not fired
      // yet.
      if (!local_topology.empty() && local_topology.front().handle == link_origin) {
        child_view_watcher->UpdateLinkStatus(ChildViewStatus::CONTENT_HAS_PRESENTED);
      }
    }
  }

  for (size_t i = 0; i < global_topology.size(); ++i) {
    const auto& handle = global_topology[i];

    // For a particular Link, the ViewportProperties and ParentViewportWatcherImpl both live on the
    // ChildLink's |graph_handle|. They can show up in either order (ViewportProperties before
    // ParentViewportWatcherImpl if the parent Flatland calls Present() first, other way around if
    // the link resolves first), so one being present without another is not a bug.
    auto graph_kv = parent_viewport_watcher_map_.find(handle);
    if (graph_kv != parent_viewport_watcher_map_.end()) {
      auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
      if (uber_struct_kv != uber_structs.end()) {
        auto properties_kv = uber_struct_kv->second->link_properties.find(handle);
        if (properties_kv != uber_struct_kv->second->link_properties.end() &&
            properties_kv->second.has_logical_size()) {
          const auto pixel_scale = display_pixel_scale * ComputeScale(global_matrices[i]);
          LayoutInfo info;
          info.set_logical_size(properties_kv->second.logical_size());
          info.set_pixel_scale(
              {static_cast<uint32_t>(pixel_scale.x), static_cast<uint32_t>(pixel_scale.y)});
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
