// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <memory>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>

using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::Error;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::ImageProperties;
using fuchsia::ui::scenic::internal::LinkProperties;
using fuchsia::ui::scenic::internal::Orientation;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {

Flatland::Flatland(scheduling::SessionId session_id,
                   const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
                   const std::shared_ptr<Renderer>& renderer,
                   const std::shared_ptr<LinkSystem>& link_system,
                   const std::shared_ptr<UberStructSystem::UberStructQueue>& uber_struct_queue,
                   fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
    : session_id_(session_id),
      flatland_presenter_(flatland_presenter),
      renderer_(renderer),
      link_system_(link_system),
      uber_struct_queue_(uber_struct_queue),
      sysmem_allocator_(std::move(sysmem_allocator)),
      transform_graph_(session_id_),
      local_root_(transform_graph_.CreateTransform()) {
  zx_status_t status = zx::event::create(0, &buffer_collection_release_fence_);
  FX_DCHECK(status == ZX_OK);
}

Flatland::~Flatland() {
  // TODO(fxbug.dev/53330): remove this function call when FrameScheduler is integrated.
  SignalBufferReleaseFence();

  // TODO(fxbug.dev/55374): consider if Link tokens should be returned or not.
}

void Flatland::Present(std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
                       PresentCallback callback) {
  auto root_handle = GetRoot();

  // TODO(fxbug.dev/40818): Decide on a proper limit on compute time for topological sorting.
  auto data = transform_graph_.ComputeAndCleanup(root_handle, std::numeric_limits<uint64_t>::max());
  FX_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());

  // TODO(fxbug.dev/36166): Once the 2D scene graph is externalized, don't commit changes if a cycle
  // is detected. Instead, kill the channel and remove the sub-graph from the global graph.
  failure_since_previous_present_ |= !data.cyclical_edges.empty();

  if (!failure_since_previous_present_) {
    FX_DCHECK(data.sorted_transforms[0].handle == root_handle);

    // Cleanup released resources.
    for (const auto& dead_handle : data.dead_transforms) {
      matrices_.erase(dead_handle);

      auto image_kv = image_metadatas_.find(dead_handle);
      if (image_kv != image_metadatas_.end()) {
        // The buffer collection metadata referenced by the image must still be alive. Decrement
        // its usage count, which may trigger garbage collection if the collection has been
        // released.
        auto buffer_data_kv = buffer_collections_.find(image_kv->second.collection_id);
        FX_DCHECK(buffer_data_kv != buffer_collections_.end());

        --buffer_data_kv->second.image_count;

        image_metadatas_.erase(image_kv);
      }
    }

    // Check if deregistered buffer collections can be garbage collected with the Renderer. Use the
    // default dispatcher since that is the same one Present() is running on.
    auto dispatcher = async_get_default_dispatcher();
    for (auto released_iter = released_buffer_collection_ids_.begin();
         released_iter != released_buffer_collection_ids_.end();) {
      const auto global_collection_id = *released_iter;
      auto buffer_data_kv = buffer_collections_.find(global_collection_id);
      FX_DCHECK(buffer_data_kv != buffer_collections_.end());

      // If a buffer collection is no longer referenced by any Images, wait on a release fence to
      // deregister it from the Renderer.
      if (buffer_data_kv->second.image_count == 0) {
        // Use a self-referencing async::WaitOnce to perform Renderer deregistration. This is
        // primarily so the handler does not have to live in the Flatland instance, which may be
        // destroyed before the release fence is signaled. WaitOnce moves the handler to the stack
        // prior to invoking it, so it is safe for the handler to delete the WaitOnce on exit.
        auto wait = std::make_shared<async::WaitOnce>(buffer_collection_release_fence_.get(),
                                                      ZX_EVENT_SIGNALED);
        zx_status_t status = wait->Begin(
            dispatcher, [copy_ref = wait, renderer_ref = renderer_, global_collection_id](
                            async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                            const zx_packet_signal_t* /*signal*/) mutable {
              FX_DCHECK(status == ZX_OK);
              renderer_ref->DeregisterCollection(global_collection_id);
            });
        FX_DCHECK(status == ZX_OK);

        // Delete local references to the GlobalBufferCollectionId.
        buffer_collections_.erase(buffer_data_kv);
        released_iter = released_buffer_collection_ids_.erase(released_iter);
      } else {
        ++released_iter;
      }
    }

    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = std::move(data.sorted_transforms);

    for (const auto& [link_id, child_link] : child_links_) {
      LinkProperties initial_properties;
      fidl::Clone(child_link.properties, &initial_properties);
      uber_struct->link_properties[child_link.link.graph_handle] = std::move(initial_properties);
    }

    for (const auto& [handle, matrix_data] : matrices_) {
      uber_struct->local_matrices[handle] = matrix_data.GetMatrix();
    }

    uber_struct->images = image_metadatas_;

    // Register a Present to get the PresentId needed to queue the UberStruct. This happens before
    // waiting on the acquire fences to indicate that a Present is pending.
    auto present_id = flatland_presenter_->RegisterPresent(session_id_, std::move(release_fences));

    // Safe to capture |this| because the Flatland is guaranteed to outlive |fence_queue_|,
    // Flatland is non-movable and FenceQueue does not fire closures after destruction.
    fence_queue_->QueueTask(
        [this, present_id, uber_struct = std::move(uber_struct),
         link_operations = std::move(pending_link_operations_),
         release_fences = std::move(release_fences)]() mutable {
          // Push the UberStruct, then schedule the associated Present that will eventually publish
          // it to the InstanceMap used for rendering.
          uber_struct_queue_->Push(present_id, std::move(uber_struct));
          flatland_presenter_->ScheduleUpdateForSession({session_id_, present_id});

          // Finalize Link destruction operations after publishing the new UberStruct. This
          // ensures that any local Transforms referenced by the to-be-deleted Links are already
          // removed from the now-published UberStruct.
          for (auto& operation : link_operations) {
            operation();
          }
        },
        std::move(acquire_fences));

    // TODO(fxbug.dev/36161): Once present operations can be pipelined, this variable will change
    // state based on the number of outstanding Present calls. Until then, this call is synchronous,
    // and we can always return 1 as the number of remaining presents.
    callback(fit::ok(num_presents_remaining_));
  } else {
    // TODO(fxbug.dev/56869): determine if pending link operations should still be run here.
    callback(fit::error(Error::BAD_OPERATION));
  }

  failure_since_previous_present_ = false;
}

void Flatland::LinkToParent(GraphLinkToken token, fidl::InterfaceRequest<GraphLink> graph_link) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    FX_LOGS(ERROR) << "LinkToParent failed, GraphLinkToken was invalid";
    ReportError();
    return;
  }

  FX_DCHECK(link_system_);

  // This portion of the method is not feed forward. This makes it possible for clients to receive
  // layout information before this operation has been presented. By initializing the link
  // immediately, parents can inform children of layout changes, and child clients can perform
  // layout decisions before their first call to Present().
  auto link_origin = transform_graph_.CreateTransform();
  LinkSystem::ParentLink link =
      link_system_->CreateParentLink(std::move(token), std::move(graph_link), link_origin);

  // This portion of the method is feed-forward. The parent-child relationship between
  // |link_origin| and |local_root_| establishes the Transform hierarchy between the two instances,
  // but the operation will not be visible until the next Present() call includes that topology.
  if (parent_link_) {
    bool child_removed = transform_graph_.RemoveChild(parent_link_->link_origin, local_root_);
    FX_DCHECK(child_removed);

    bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
    FX_DCHECK(transform_released);
  }

  bool child_added = transform_graph_.AddChild(link.link_origin, local_root_);
  FX_DCHECK(child_added);
  parent_link_ = std::move(link);
}

void Flatland::UnlinkFromParent(
    fuchsia::ui::scenic::internal::Flatland::UnlinkFromParentCallback callback) {
  if (!parent_link_) {
    FX_LOGS(ERROR) << "UnlinkFromParent failed, no existing parent Link";
    ReportError();
    return;
  }

  // Deleting the old ParentLink's Transform effectively changes this intance's root back to
  // |local_root_|.
  bool child_removed = transform_graph_.RemoveChild(parent_link_->link_origin, local_root_);
  FX_DCHECK(child_removed);

  bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
  FX_DCHECK(transform_released);

  // Move the old parent link into the delayed operation so that it isn't taken into account when
  // computing the local topology, but doesn't get deleted until after the new UberStruct is
  // published.
  auto local_link = std::move(parent_link_.value());
  parent_link_.reset();

  // Delay the actual destruction of the Link until the next Present().
  pending_link_operations_.push_back(
      [local_link = std::move(local_link), callback = std::move(callback)]() mutable {
        GraphLinkToken return_token;

        // If the link is still valid, return the original token. If not, create an orphaned
        // zx::eventpair and return it since the ObjectLinker does not retain the orphaned token.
        auto link_token = local_link.exporter.ReleaseToken();
        if (link_token.has_value()) {
          return_token.value = zx::eventpair(std::move(link_token.value()));
        } else {
          // |peer_token| immediately falls out of scope, orphaning |return_token|.
          zx::eventpair peer_token;
          zx::eventpair::create(0, &return_token.value, &peer_token);
        }

        callback(std::move(return_token));
      });
}

void Flatland::ClearGraph() {
  // Clear user-defined mappings and local matrices.
  transforms_.clear();
  content_handles_.clear();
  buffer_collection_ids_.clear();
  matrices_.clear();

  // List all global buffer collection IDs as "released", which will trigger cleanup in Present().
  for (const auto& [collection_id, buffer_collection] : buffer_collections_) {
    released_buffer_collection_ids_.insert(collection_id);
  }

  // We always preserve the link origin when clearing the graph. This call will place all other
  // TransformHandles in the dead_transforms set in the next Present(), which will trigger cleanup
  // of Images and BufferCollections.
  transform_graph_.ResetGraph(local_root_);

  // If a parent Link exists, delay its destruction until Present().
  if (parent_link_.has_value()) {
    auto local_link = std::move(parent_link_);
    parent_link_.reset();

    pending_link_operations_.push_back(
        [local_link = std::move(local_link)]() mutable { local_link.reset(); });
  }

  // Delay destruction of all child Links until Present().
  auto local_links = std::move(child_links_);
  child_links_.clear();

  pending_link_operations_.push_back(
      [local_links = std::move(local_links)]() mutable { local_links.clear(); });
}

void Flatland::CreateTransform(TransformId transform_id) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "CreateTransform called with transform_id 0";
    ReportError();
    return;
  }

  if (transforms_.count(transform_id)) {
    FX_LOGS(ERROR) << "CreateTransform called with pre-existing transform_id " << transform_id;
    ReportError();
    return;
  }

  TransformHandle handle = transform_graph_.CreateTransform();
  transforms_.insert({transform_id, handle});
}

void Flatland::SetTranslation(TransformId transform_id, Vec2 translation) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "SetTranslation called with transform_id 0";
    ReportError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id);

  if (transform_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "SetTranslation failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  matrices_[transform_kv->second].SetTranslation(translation);
}

void Flatland::SetOrientation(TransformId transform_id, Orientation orientation) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "SetOrientation called with transform_id 0";
    ReportError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id);

  if (transform_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "SetOrientation failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  matrices_[transform_kv->second].SetOrientation(orientation);
}

void Flatland::SetScale(TransformId transform_id, Vec2 scale) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "SetScale called with transform_id 0";
    ReportError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id);

  if (transform_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "SetScale failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  matrices_[transform_kv->second].SetScale(scale);
}

void Flatland::AddChild(TransformId parent_transform_id, TransformId child_transform_id) {
  if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "AddChild called with transform_id zero";
    ReportError();
    return;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id);
  auto child_global_kv = transforms_.find(child_transform_id);

  if (parent_global_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "AddChild failed, parent_transform_id " << parent_transform_id
                   << " not found";
    ReportError();
    return;
  }

  if (child_global_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "AddChild failed, child_transform_id " << child_transform_id << " not found";
    ReportError();
    return;
  }

  bool added = transform_graph_.AddChild(parent_global_kv->second, child_global_kv->second);

  if (!added) {
    FX_LOGS(ERROR) << "AddChild failed, connection already exists between parent "
                   << parent_transform_id << " and child " << child_transform_id;
    ReportError();
  }
}

void Flatland::RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) {
  if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "RemoveChild failed, transform_id " << parent_transform_id << " not found";
    ReportError();
    return;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id);
  auto child_global_kv = transforms_.find(child_transform_id);

  if (parent_global_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "RemoveChild failed, parent_transform_id " << parent_transform_id
                   << " not found";
    ReportError();
    return;
  }

  if (child_global_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "RemoveChild failed, child_transform_id " << child_transform_id
                   << " not found";
    ReportError();
    return;
  }

  bool removed = transform_graph_.RemoveChild(parent_global_kv->second, child_global_kv->second);

  if (!removed) {
    FX_LOGS(ERROR) << "RemoveChild failed, connection between parent " << parent_transform_id
                   << " and child " << child_transform_id << " not found";
    ReportError();
  }
}

void Flatland::SetRootTransform(TransformId transform_id) {
  // SetRootTransform(0) is special -- it only clears the existing root transform.
  if (transform_id == kInvalidId) {
    transform_graph_.ClearChildren(local_root_);
    return;
  }

  auto global_kv = transforms_.find(transform_id);
  if (global_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  transform_graph_.ClearChildren(local_root_);

  bool added = transform_graph_.AddChild(local_root_, global_kv->second);
  FX_DCHECK(added);
}

void Flatland::CreateLink(ContentId link_id, ContentLinkToken token, LinkProperties properties,
                          fidl::InterfaceRequest<ContentLink> content_link) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    FX_LOGS(ERROR) << "CreateLink failed, ContentLinkToken was invalid";
    ReportError();
    return;
  }

  if (!properties.has_logical_size()) {
    FX_LOGS(ERROR) << "CreateLink must be provided a LinkProperties with a logical size";
    ReportError();
    return;
  }

  auto logical_size = properties.logical_size();
  if (logical_size.x <= 0.f || logical_size.y <= 0.f) {
    FX_LOGS(ERROR) << "CreateLink must be provided a logical size with positive X and Y values";
    ReportError();
    return;
  }

  FX_DCHECK(link_system_);

  // The LinkProperties and ContentLinkImpl live on a handle from this Flatland instance.
  auto graph_handle = transform_graph_.CreateTransform();

  // We can initialize the Link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method. We also forward the initial LinkProperties through
  // the LinkSystem immediately, so the child can receive them as soon as possible.
  LinkProperties initial_properties;
  fidl::Clone(properties, &initial_properties);
  LinkSystem::ChildLink link = link_system_->CreateChildLink(
      std::move(token), std::move(initial_properties), std::move(content_link), graph_handle);

  if (link_id == 0) {
    FX_LOGS(ERROR) << "CreateLink called with ContentId zero";
    ReportError();
    return;
  }

  if (content_handles_.count(link_id)) {
    FX_LOGS(ERROR) << "CreateLink called with existing ContentId " << link_id;
    ReportError();
    return;
  }

  // This is the feed-forward portion of the method. Here, we add the link to the map, and
  // initialize its layout with the desired properties. The Link will not actually result in
  // additions to the Transform hierarchy until it is added to a Transform.
  bool child_added = transform_graph_.AddChild(link.graph_handle, link.link_handle);
  FX_DCHECK(child_added);

  // Default the link size to the logical size, which is just an identity scale matrix, so
  // that future logical size changes will result in the correct scale matrix.
  Vec2 size = properties.logical_size();

  content_handles_[link_id] = link.graph_handle;
  child_links_[link.graph_handle] = {
      .link = std::move(link), .properties = std::move(properties), .size = std::move(size)};
}

void Flatland::RegisterBufferCollection(
    BufferCollectionId collection_id,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(renderer_);

  if (collection_id == 0) {
    FX_LOGS(ERROR) << "RegisterBufferCollection called with collection_id 0";
    ReportError();
    return;
  }

  if (buffer_collection_ids_.count(collection_id)) {
    FX_LOGS(ERROR) << "RegisterBufferCollection called with pre-existing collection_id "
                   << collection_id;
    ReportError();
    return;
  }

  // Register the texture collection immediately since the client may block on buffers being
  // allocated before calling Present().
  auto global_collection_id =
      renderer_->RegisterTextureCollection(sysmem_allocator_.get(), std::move(token));

  // But don't allow the collection to be referenced in other function calls until presented.
  if (global_collection_id == Renderer::kInvalidId) {
    FX_LOGS(ERROR)
        << "RegisterBufferCollection failed to register the sysmem token with the renderer.";
    ReportError();
    return;
  }
  FX_DCHECK(!buffer_collections_.count(global_collection_id));

  buffer_collection_ids_[collection_id] = global_collection_id;
  buffer_collections_[global_collection_id] = BufferCollectionData();
}

void Flatland::CreateImage(ContentId image_id, BufferCollectionId collection_id, uint32_t vmo_index,
                           ImageProperties properties) {
  FX_DCHECK(renderer_);

  if (image_id == 0) {
    FX_LOGS(ERROR) << "CreateImage called with image_id 0";
    ReportError();
    return;
  }

  if (content_handles_.count(image_id)) {
    FX_LOGS(ERROR) << "CreateImage called with pre-existing image_id " << image_id;
    ReportError();
    return;
  }

  auto buffer_id_kv = buffer_collection_ids_.find(collection_id);

  if (buffer_id_kv == buffer_collection_ids_.end()) {
    FX_LOGS(ERROR) << "CreateImage failed, collection_id " << collection_id << " not found.";
    ReportError();
    return;
  }

  const auto global_collection_id = buffer_id_kv->second;

  auto buffer_collection_kv = buffer_collections_.find(global_collection_id);
  FX_DCHECK(buffer_collection_kv != buffer_collections_.end());

  auto& buffer_data = buffer_collection_kv->second;

  // If the buffer hasn't been validated yet, try to validate it. If validation fails, it will be
  // impossible to render this image.
  if (!buffer_data.metadata.has_value()) {
    auto metadata = renderer_->Validate(global_collection_id);
    if (!metadata.has_value()) {
      FX_LOGS(ERROR) << "CreateImage failed, collection_id " << collection_id
                     << " has not been allocated yet";
      ReportError();
      return;
    }

    buffer_data.metadata = std::move(metadata.value());
  }

  if (vmo_index >= buffer_data.metadata->vmo_count) {
    FX_LOGS(ERROR) << "CreateImage failed, vmo_index " << vmo_index
                   << " must be less than vmo_count " << buffer_data.metadata->vmo_count;
    ReportError();
    return;
  }

  const auto& image_constraints = buffer_data.metadata->image_constraints;

  if (!properties.has_width()) {
    FX_LOGS(ERROR) << "CreateImage failed, ImageProperties did not specify a width";
    ReportError();
    return;
  }

  const uint32_t width = properties.width();
  if (width < image_constraints.min_coded_width || width > image_constraints.max_coded_width) {
    FX_LOGS(ERROR) << "CreateImage failed, width " << width << " is not within valid range ["
                   << image_constraints.min_coded_width << "," << image_constraints.max_coded_width
                   << "]";
    ReportError();
    return;
  }

  if (!properties.has_height()) {
    FX_LOGS(ERROR) << "CreateImage failed, ImageProperties did not specify a height";
    ReportError();
    return;
  }

  const uint32_t height = properties.height();
  if (height < image_constraints.min_coded_height || height > image_constraints.max_coded_height) {
    FX_LOGS(ERROR) << "CreateImage failed, height " << height << " is not within valid range ["
                   << image_constraints.min_coded_height << ","
                   << image_constraints.max_coded_height << "]";
    ReportError();
    return;
  }

  auto handle = transform_graph_.CreateTransform();
  content_handles_[image_id] = handle;

  auto& metadata = image_metadatas_[handle];
  metadata.collection_id = global_collection_id;
  metadata.vmo_idx = vmo_index;
  metadata.width = width;
  metadata.height = height;

  // Increment the buffer's usage count.
  ++buffer_data.image_count;
}

void Flatland::SetContentOnTransform(ContentId content_id, TransformId transform_id) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "SetContentOnTransform called with transform_id zero";
    ReportError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id);

  if (transform_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "SetContentOnTransform failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  if (content_id == 0) {
    transform_graph_.ClearPriorityChild(transform_kv->second);
    return;
  }

  auto handle_kv = content_handles_.find(content_id);

  if (handle_kv == content_handles_.end()) {
    FX_LOGS(ERROR) << "SetContentOnTransform failed, content_id " << content_id << " not found";
    ReportError();
    return;
  }

  transform_graph_.SetPriorityChild(transform_kv->second, handle_kv->second);
}

void Flatland::SetLinkProperties(ContentId link_id, LinkProperties properties) {
  if (link_id == 0) {
    FX_LOGS(ERROR) << "SetLinkProperties called with link_id zero.";
    ReportError();
    return;
  }

  auto content_kv = content_handles_.find(link_id);

  if (content_kv == content_handles_.end()) {
    FX_LOGS(ERROR) << "SetLinkProperties failed, link_id " << link_id << " not found";
    ReportError();
    return;
  }

  auto link_kv = child_links_.find(content_kv->second);

  if (link_kv == child_links_.end()) {
    FX_LOGS(ERROR) << "SetLinkProperties failed, content_id " << link_id << " is not a Link";
    ReportError();
    return;
  }

  // Callers do not have to provide a new logical size on every call to SetLinkProperties, but if
  // they do, it must have positive X and Y values.
  if (properties.has_logical_size()) {
    auto logical_size = properties.logical_size();
    if (logical_size.x <= 0.f || logical_size.y <= 0.f) {
      FX_LOGS(ERROR) << "SetLinkProperties failed, logical_size components must be positive, "
                     << "given (" << logical_size.x << ", " << logical_size.y << ")";
      ReportError();
      return;
    }
  } else {
    // Preserve the old logical size if no logical size was passed as an argument. The
    // HangingGetHelper no-ops if no data changes, so if logical size is empty and no other
    // properties changed, the hanging get won't fire.
    properties.set_logical_size(link_kv->second.properties.logical_size());
  }

  FX_DCHECK(link_kv->second.link.importer.valid());

  link_kv->second.properties = std::move(properties);
  UpdateLinkScale(link_kv->second);
}

void Flatland::SetLinkSize(ContentId link_id, Vec2 size) {
  if (link_id == 0) {
    FX_LOGS(ERROR) << "SetLinkSize called with link_id zero";
    ReportError();
    return;
  }

  if (size.x <= 0.f || size.y <= 0.f) {
    FX_LOGS(ERROR) << "SetLinkSize failed, size components must be positive, given (" << size.x
                   << ", " << size.y << ")";
    ReportError();
    return;
  }

  auto content_kv = content_handles_.find(link_id);

  if (content_kv == content_handles_.end()) {
    FX_LOGS(ERROR) << "SetLinkSize failed, link_id " << link_id << " not found";
    ReportError();
    return;
  }

  auto link_kv = child_links_.find(content_kv->second);

  if (link_kv == child_links_.end()) {
    FX_LOGS(ERROR) << "SetLinkSize failed, content_id " << link_id << " is not a Link";
    ReportError();
    return;
  }

  FX_DCHECK(link_kv->second.link.importer.valid());

  link_kv->second.size = std::move(size);
  UpdateLinkScale(link_kv->second);
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  if (transform_id == kInvalidId) {
    FX_LOGS(ERROR) << "ReleaseTransform called with transform_id zero";
    ReportError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id);

  if (transform_kv == transforms_.end()) {
    FX_LOGS(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";
    ReportError();
    return;
  }

  bool erased_from_graph = transform_graph_.ReleaseTransform(transform_kv->second);
  FX_DCHECK(erased_from_graph);
  transforms_.erase(transform_kv);
}

void Flatland::ReleaseLink(ContentId link_id,
                           fuchsia::ui::scenic::internal::Flatland::ReleaseLinkCallback callback) {
  if (link_id == 0) {
    FX_LOGS(ERROR) << "ReleaseLink called with link_id zero";
    ReportError();
    return;
  }

  auto content_kv = content_handles_.find(link_id);

  if (content_kv == content_handles_.end()) {
    FX_LOGS(ERROR) << "ReleaseLink failed, link_id " << link_id << " not found";
    ReportError();
    return;
  }

  auto link_kv = child_links_.find(content_kv->second);

  if (link_kv == child_links_.end()) {
    FX_LOGS(ERROR) << "ReleaseLink failed, content_id " << link_id << " is not a Link";
    ReportError();
    return;
  }

  // Deleting the ChildLink's |graph_handle| effectively deletes the link from the local topology,
  // even if the link object itself is not deleted.
  bool child_removed = transform_graph_.RemoveChild(link_kv->second.link.graph_handle,
                                                    link_kv->second.link.link_handle);
  FX_DCHECK(child_removed);

  bool content_released = transform_graph_.ReleaseTransform(link_kv->second.link.graph_handle);
  FX_DCHECK(content_released);

  // Move the old child link into the delayed operation so that the ContentId is immeditely free
  // for re-use, but it doesn't get deleted until after the new UberStruct is published.
  auto child_link = std::move(link_kv->second);
  child_links_.erase(content_kv->second);
  content_handles_.erase(content_kv);

  // Delay the actual destruction of the link until the next Present().
  pending_link_operations_.push_back(
      [child_link = std::move(child_link), callback = std::move(callback)]() mutable {
        ContentLinkToken return_token;

        // If the link is still valid, return the original token. If not, create an orphaned
        // zx::eventpair and return it since the ObjectLinker does not retain the orphaned token.
        auto link_token = child_link.link.importer.ReleaseToken();
        if (link_token.has_value()) {
          return_token.value = zx::eventpair(std::move(link_token.value()));
        } else {
          // |peer_token| immediately falls out of scope, orphaning |return_token|.
          zx::eventpair peer_token;
          zx::eventpair::create(0, &return_token.value, &peer_token);
        }

        callback(std::move(return_token));
      });
}

void Flatland::DeregisterBufferCollection(BufferCollectionId collection_id) {
  if (collection_id == kInvalidId) {
    FX_LOGS(ERROR) << "DeregisterBufferCollection called with collection_id zero";
    ReportError();
    return;
  }

  auto buffer_id_kv = buffer_collection_ids_.find(collection_id);

  if (buffer_id_kv == buffer_collection_ids_.end()) {
    FX_LOGS(ERROR) << "DeregisterBufferCollection failed, collection_id " << collection_id
                   << " not found";
    ReportError();
    return;
  }

  auto global_collection_id = buffer_id_kv->second;
  FX_DCHECK(buffer_collections_.count(global_collection_id));

  // Erase the user-facing mapping of the ID and queue the global ID for garbage collection. The
  // actual buffer collection data will be cleared once all Images reference the collection are
  // released and garbage collected.
  buffer_collection_ids_.erase(buffer_id_kv);
  released_buffer_collection_ids_.insert(global_collection_id);
}

void Flatland::ReleaseImage(ContentId image_id) {
  if (image_id == kInvalidId) {
    FX_LOGS(ERROR) << "ReleaseImage called with image_id zero";
    ReportError();
    return;
  }

  auto content_kv = content_handles_.find(image_id);

  if (content_kv == content_handles_.end()) {
    FX_LOGS(ERROR) << "ReleaseImage failed, image_id " << image_id << " not found";
    ReportError();
    return;
  }

  auto image_kv = image_metadatas_.find(content_kv->second);

  if (image_kv == image_metadatas_.end()) {
    FX_LOGS(ERROR) << "ReleaseImage failed, content_id " << image_id << " is not an Image";
    ReportError();
    return;
  }

  bool erased_from_graph = transform_graph_.ReleaseTransform(content_kv->second);
  FX_DCHECK(erased_from_graph);

  // Even though the handle is released, it may still be referenced by client Transforms. The
  // image_metadatas_ map preserves the entry until it shows up in the dead_transforms list.
  content_handles_.erase(image_id);
}

TransformHandle Flatland::GetRoot() const {
  return parent_link_ ? parent_link_->link_origin : local_root_;
}

void Flatland::SignalBufferReleaseFence() {
  buffer_collection_release_fence_.signal(0, ZX_EVENT_SIGNALED);

  // Recreate the release fence since an event cannot be unsignaled.
  zx_status_t status = zx::event::create(0, &buffer_collection_release_fence_);
  FX_DCHECK(status == ZX_OK);
}

void Flatland::ReportError() { failure_since_previous_present_ = true; }

void Flatland::UpdateLinkScale(const ChildLinkData& link_data) {
  FX_DCHECK(link_data.properties.has_logical_size());

  auto logical_size = link_data.properties.logical_size();
  matrices_[link_data.link.graph_handle].SetScale(
      {link_data.size.x / logical_size.x, link_data.size.y / logical_size.y});
}

// MatrixData function implementations

// static
float Flatland::MatrixData::GetOrientationAngle(
    fuchsia::ui::scenic::internal::Orientation orientation) {
  switch (orientation) {
    case Orientation::CCW_0_DEGREES:
      return 0.f;
    case Orientation::CCW_90_DEGREES:
      return glm::half_pi<float>();
    case Orientation::CCW_180_DEGREES:
      return glm::pi<float>();
    case Orientation::CCW_270_DEGREES:
      return glm::three_over_two_pi<float>();
  }
}

void Flatland::MatrixData::SetTranslation(fuchsia::ui::scenic::internal::Vec2 translation) {
  translation_.x = translation.x;
  translation_.y = translation.y;

  RecomputeMatrix();
}

void Flatland::MatrixData::SetOrientation(fuchsia::ui::scenic::internal::Orientation orientation) {
  angle_ = GetOrientationAngle(orientation);

  RecomputeMatrix();
}

void Flatland::MatrixData::SetScale(fuchsia::ui::scenic::internal::Vec2 scale) {
  scale_.x = scale.x;
  scale_.y = scale.y;

  RecomputeMatrix();
}

void Flatland::MatrixData::RecomputeMatrix() {
  // Manually compose the matrix rather than use glm transformations since the order of operations
  // is always the same. glm matrices are column-major.
  float* vals = static_cast<float*>(glm::value_ptr(matrix_));

  // Translation in the third column.
  vals[6] = translation_.x;
  vals[7] = translation_.y;

  // Rotation and scale combined into the first two columns.
  const float s = sin(angle_);
  const float c = cos(angle_);

  vals[0] = c * scale_.x;
  vals[1] = s * scale_.x;
  vals[3] = -1.f * s * scale_.y;
  vals[4] = c * scale_.y;
}

glm::mat3 Flatland::MatrixData::GetMatrix() const { return matrix_; }

}  // namespace flatland
