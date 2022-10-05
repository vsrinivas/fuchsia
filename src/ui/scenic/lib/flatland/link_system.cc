// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/link_system.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/dispatcher_holder.h"
#include "src/ui/scenic/lib/utils/task_utils.h"

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

// Scale can be extracted from a matrix by finding the length of the
// column the scale is located in:
//
//   a b c
//   e f g
//   i j k
//
// If |a| is the x scale and rotation, and |f| is the y scale and rotation, then
// we can calculate the x scale with length(vector(a,e,i)) and y scale with
// length(vector(b,f,j)).
glm::vec2 ComputeScale(const glm::mat3& matrix) {
  const glm::vec3 x_column = glm::column(matrix, 0);
  const glm::vec3 y_column = glm::column(matrix, 1);
  return {glm::length(x_column), glm::length(y_column)};
}

}  // namespace

LinkSystem::LinkSystem(TransformHandle::InstanceId instance_id)
    : instance_id_(instance_id), link_graph_(instance_id_), linker_(ObjectLinker::New()) {}

LinkSystem::LinkToChild LinkSystem::CreateLinkToChild(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder, ViewportCreationToken token,
    fuchsia::ui::composition::ViewportProperties initial_properties,
    fidl::InterfaceRequest<ChildViewWatcher> child_view_watcher,
    TransformHandle parent_transform_handle, LinkProtocolErrorCallback error_callback) {
  FX_DCHECK(token.value.is_valid());
  FX_DCHECK(initial_properties.has_logical_size());
  FX_DCHECK(initial_properties.has_inset());

  auto impl = std::make_shared<ChildViewWatcherImpl>(dispatcher_holder,
                                                     std::move(child_view_watcher), error_callback);
  const TransformHandle internal_link_handle = CreateTransformLocked();

  ObjectLinker::ImportLink importer = linker_->CreateImport(
      LinkToChildInfo{.parent_transform_handle = parent_transform_handle,
                      .internal_link_handle = internal_link_handle,
                      .initial_logical_size = initial_properties.logical_size(),
                      .initial_inset = initial_properties.inset()},
      std::move(token.value),
      /* error_reporter */ nullptr);

  auto child_transform_handle = std::make_shared<TransformHandle>();  // Uninitialized.
  importer.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl, child_transform_handle](LinkToParentInfo info) mutable {
        if (info.view_ref != nullptr) {
          impl->SetViewRef({.reference = utils::CopyEventpair(info.view_ref->reference)});
        }

        *child_transform_handle = info.child_transform_handle;
        {
          std::scoped_lock lock(ref->mutex_);
          ref->child_to_parent_map_[*child_transform_handle] =
              ParentEnd{.child_view_watcher = impl};
        }
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl, child_transform_handle,
       weak_dispatcher_holder = std::weak_ptr<utils::DispatcherHolder>(dispatcher_holder)](
          bool on_link_destruction) mutable {
        // We expect |child_transform_handle| to be assigned by the "link_resolved" closure,
        // but this might not happen if the link is being destroyed before it was resolved.
        FX_DCHECK(child_transform_handle || on_link_destruction);

        {
          std::scoped_lock lock(ref->mutex_);
          ref->child_to_parent_map_.erase(*child_transform_handle);
        }

        // Avoid race conditions by destroying ParentViewportWatcher on its "own" thread.  For
        // example, if not destroyed on its "own" thread, it might concurrently be handling a FIDL
        // message.
        if (auto dispatcher_holder = weak_dispatcher_holder.lock()) {
          utils::ExecuteOrPostTaskOnDispatcher(
              dispatcher_holder->dispatcher(),
              [impl = std::move(impl)]() mutable { impl.reset(); });

          // The point of moving |impl| into the task above is to destroy it on the correct thread.
          // Verify that we did actually move it (previously, there was a subtle bug where this
          // closure wasn't declared as mutable, so we copied the shared_ptr instead of moving it).
          FX_DCHECK(!impl);
        }
      });

  return LinkToChild({
      .parent_transform_handle = parent_transform_handle,
      .internal_link_handle = internal_link_handle,
      .importer = std::move(importer),
  });
}

LinkSystem::LinkToParent LinkSystem::CreateLinkToParent(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder, ViewCreationToken token,
    std::optional<fuchsia::ui::views::ViewIdentityOnCreation> view_identity,
    fidl::InterfaceRequest<ParentViewportWatcher> parent_viewport_watcher,
    TransformHandle child_transform_handle, LinkProtocolErrorCallback error_callback) {
  FX_DCHECK(token.value.is_valid());

  std::shared_ptr<fuchsia::ui::views::ViewRef> view_ref;
  std::optional<fuchsia::ui::views::ViewRefControl> view_ref_control;
  if (view_identity.has_value()) {
    view_ref = std::make_shared<fuchsia::ui::views::ViewRef>(std::move(view_identity->view_ref));
    view_ref_control = std::move(view_identity->view_ref_control);
  }

  auto impl = std::make_shared<ParentViewportWatcherImpl>(
      dispatcher_holder, std::move(parent_viewport_watcher), error_callback);

  ObjectLinker::ExportLink exporter = linker_->CreateExport(
      LinkToParentInfo{.child_transform_handle = child_transform_handle, .view_ref = view_ref},
      std::move(token.value),
      /* error_reporter */ nullptr);

  auto parent_transform_handle = std::make_shared<TransformHandle>();  // Uninitialized.
  auto topology_map_key = std::make_shared<TransformHandle>();         // Uninitialized.
  exporter.Initialize(
      /* link_resolved = */
      [ref = shared_from_this(), impl, parent_transform_handle, topology_map_key,
       child_transform_handle, dpr = initial_device_pixel_ratio_](LinkToChildInfo info) {
        *parent_transform_handle = info.parent_transform_handle;
        *topology_map_key = info.internal_link_handle;

        {
          std::scoped_lock lock(ref->mutex_);
          // TODO(fxbug.dev/80603): When the same parent relinks to different children, we might be
          // using an outdated logical_size here. It will be corrected in UpdateLinks(), but we
          // should figure out a way to set the previous ParentViewportWatcherImpl's size here.
          LayoutInfo layout_info;
          layout_info.set_logical_size(info.initial_logical_size);
          layout_info.set_pixel_scale({1, 1});
          layout_info.set_device_pixel_ratio({dpr.x, dpr.y});
          layout_info.set_inset(info.initial_inset);
          impl->UpdateLayoutInfo(std::move(layout_info));

          ref->parent_to_child_map_[*parent_transform_handle] = ChildEnd{
              .parent_viewport_watcher = impl, .child_transform_handle = child_transform_handle};
          // The topology is constructed here, instead of in the link_resolved closure of the
          // LinkToParent object, so that its destruction (which depends on the
          // internal_link_handle) can occur on the same endpoint.
          ref->link_topologies_[*topology_map_key] = child_transform_handle;
        }
      },
      /* link_invalidated = */
      [ref = shared_from_this(), impl, parent_transform_handle, topology_map_key,
       weak_dispatcher_holder = std::weak_ptr<utils::DispatcherHolder>(dispatcher_holder)](
          bool on_link_destruction) mutable {
        // We expect |parent_transform_handle| and |topology_map_key| to be assigned by
        // the "link_resolved" closure, but this might not happen if the link is being destroyed
        // before it was resolved.
        FX_DCHECK((parent_transform_handle && topology_map_key) || on_link_destruction);

        {
          std::scoped_lock map_lock(ref->mutex_);
          ref->parent_to_child_map_.erase(*parent_transform_handle);

          ref->link_topologies_.erase(*topology_map_key);
          ref->link_graph_.ReleaseTransform(*topology_map_key);
        }

        // Avoid race conditions by destroying ParentViewportWatcher on its "own" thread.  For
        // example, if not destroyed on its "own" thread, it might concurrently be handling a FIDL
        // message.
        if (auto dispatcher_holder = weak_dispatcher_holder.lock()) {
          utils::ExecuteOrPostTaskOnDispatcher(
              dispatcher_holder->dispatcher(),
              [impl = std::move(impl)]() mutable { impl.reset(); });

          // The point of moving |impl| into the task above is to destroy it on the correct thread.
          // Verify that we did actually move it (previously, there was a subtle bug where this
          // closure wasn't declared as mutable, so we copied the shared_ptr instead of moving it).
          FX_DCHECK(!impl);
        }
      });

  return LinkToParent({.child_transform_handle = child_transform_handle,
                       .exporter = std::move(exporter),
                       .view_ref = std::move(view_ref),
                       .view_ref_control = std::move(view_ref_control)});
}

void LinkSystem::UpdateLinks(const GlobalTopologyData::TopologyVector& global_topology,
                             const std::unordered_set<TransformHandle>& live_handles,
                             const GlobalMatrixVector& global_matrices,
                             const glm::vec2& device_pixel_ratio,
                             const UberStruct::InstanceMap& uber_structs) {
  std::scoped_lock lock(mutex_);

  // Since the global topology may not contain every Flatland instance, manually update the
  // ParentViewportStatus of every ParentViewportWatcher.
  for (auto& [_, child_end] : parent_to_child_map_) {
    // The child Flatland instance is connected to the display if it is present in the global
    // topology.
    child_end.parent_viewport_watcher->UpdateLinkStatus(
        live_handles.count(child_end.child_transform_handle) > 0
            ? ParentViewportStatus::CONNECTED_TO_DISPLAY
            : ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);
  }

  // ChildViewWatcher has two hanging get methods, GetStatus() and GetViewRef(), whose responses are
  // generated in the loop below.
  for (auto& [child_transform_handle, parent_end] : child_to_parent_map_) {
    auto& child_view_watcher = parent_end.child_view_watcher;
    // The ChildViewStatus changes the first time the child presents with a particular parent link.
    // This is indicated by an UberStruct with the |child_transform_handle| as its first
    // TransformHandle in the snapshot.
    //
    // NOTE: This does not mean the child content is actually appears on-screen; it simply informs
    //       the parent that the child has content that is available to present on screen.  This is
    //       intentional; for example, the parent might not want to attach the child to the global
    //       scene graph until it knows the child is ready to present content on screen.
    //
    // NOTE: The LinkSystem can technically "miss" updating the ChildViewStatus for a
    //       particular ChildViewWatcher if the child presents two CreateView() calls before
    //       UpdateLinks() is called, but in that case, the first Link is destroyed, and therefore
    //       its status does not need to be updated anyway.
    auto uber_struct_kv = uber_structs.find(child_transform_handle.GetInstanceId());
    if (uber_struct_kv != uber_structs.end()) {
      const auto& local_topology = uber_struct_kv->second->local_topology;

      // If the local topology doesn't start with the |child_transform_handle|, the child is linked
      // to a different parent now, but the link_invalidated callback to remove this entry has not
      // fired yet.
      if (!local_topology.empty() && local_topology.front().handle == child_transform_handle) {
        child_view_watcher->UpdateLinkStatus(ChildViewStatus::CONTENT_HAS_PRESENTED);
      }
    }

    // As soon as the child view is part of the global topology, update the watcher to send it along
    // to any caller of GetViewRef().  For example, this means that by the time the watcher receives
    // it, the child view will already exist in the view tree, and therefore an attempt to focus it
    // will succeed.
    if (live_handles.count(child_transform_handle) > 0) {
      child_view_watcher->UpdateViewRef();
    }
  }

  std::unordered_map<std::shared_ptr<ParentViewportWatcherImpl>, LayoutInfo> layout_map;
  for (size_t i = 0; i < global_topology.size(); ++i) {
    const auto& handle = global_topology[i];

    // For a particular Link, the ViewportProperties and ParentViewportWatcherImpl both live on the
    // LinkToChild's |graph_handle|. They can show up in either order (ViewportProperties before
    // ParentViewportWatcherImpl if the parent Flatland calls Present() first, other way around if
    // the link resolves first), so one being present without another is not a bug.
    if (auto parent_to_child_kv = parent_to_child_map_.find(handle);
        parent_to_child_kv != parent_to_child_map_.end()) {
      auto uber_struct_kv = uber_structs.find(handle.GetInstanceId());
      if (uber_struct_kv != uber_structs.end()) {
        auto properties_kv = uber_struct_kv->second->link_properties.find(handle);
        if (properties_kv != uber_struct_kv->second->link_properties.end() &&
            properties_kv->second.has_logical_size()) {
          const auto pixel_scale = device_pixel_ratio * ComputeScale(global_matrices[i]);
          LayoutInfo info;
          info.set_logical_size(properties_kv->second.logical_size());
          info.set_pixel_scale(
              {static_cast<uint32_t>(pixel_scale.x), static_cast<uint32_t>(pixel_scale.y)});
          info.set_device_pixel_ratio({device_pixel_ratio.x, device_pixel_ratio.y});
          info.set_inset(properties_kv->second.inset());

          // A transform handle may have multiple parents, resulting in the same handle appearing
          // in the global topology vector multiple times, with multiple global matrices. We only
          // want to update the LayoutInfo for the instance that has the lowest scale value.
          const auto& watcher = parent_to_child_kv->second.parent_viewport_watcher;
          if (layout_map.find(watcher) == layout_map.end()) {
            layout_map[watcher] = std::move(info);
          } else {
            const auto& curr_info = layout_map[watcher];
            if (curr_info.pixel_scale().width > info.pixel_scale().width) {
              layout_map[watcher] = std::move(info);
            }
          }
        }
      }
    }
  }

  // Now that we've determined which layout information to associate with a
  // ParentViewportWatcherImpl, we can now update each one.
  for (auto& [watcher, info] : layout_map) {
    watcher->UpdateLayoutInfo(std::move(info));
  }
}

GlobalTopologyData::LinkTopologyMap LinkSystem::GetResolvedTopologyLinks() {
  GlobalTopologyData::LinkTopologyMap copy;

  // Acquire the lock and copy.
  {
    std::scoped_lock lock(mutex_);
    copy = link_topologies_;
  }
  return copy;
}

TransformHandle::InstanceId LinkSystem::GetInstanceId() const { return instance_id_; }

std::unordered_map<TransformHandle, TransformHandle> const
LinkSystem::GetLinkChildToParentTransformMap() {
  std::unordered_map<TransformHandle, TransformHandle> child_to_parent_map;
  for (const auto& [parent_transform_handle, child_end] : parent_to_child_map_) {
    child_to_parent_map.try_emplace(child_end.child_transform_handle, parent_transform_handle);
  }
  return child_to_parent_map;
}

}  // namespace flatland
