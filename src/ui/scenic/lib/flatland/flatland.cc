// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/eventpair.h>

#include <functional>
#include <memory>
#include <sstream>
#include <utility>

#include "src/lib/fsl/handles/object_info.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>

using fuchsia::math::RectF;
using fuchsia::math::SizeU;
using fuchsia::math::Vec;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::FlatlandError;
using fuchsia::ui::composition::ImageProperties;
using fuchsia::ui::composition::OnNextFrameBeginValues;
using fuchsia::ui::composition::Orientation;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace flatland {

std::shared_ptr<Flatland> Flatland::New(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
    fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
    scheduling::SessionId session_id, std::function<void()> destroy_instance_function,
    std::shared_ptr<FlatlandPresenter> flatland_presenter, std::shared_ptr<LinkSystem> link_system,
    std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
    const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
        buffer_collection_importers) {
  return std::shared_ptr<Flatland>(new Flatland(
      std::move(dispatcher_holder), std::move(request), session_id,
      std::move(destroy_instance_function), std::move(flatland_presenter), std::move(link_system),
      std::move(uber_struct_queue), buffer_collection_importers));
}

Flatland::Flatland(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                   fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
                   scheduling::SessionId session_id,
                   std::function<void()> destroy_instance_function,
                   std::shared_ptr<FlatlandPresenter> flatland_presenter,
                   std::shared_ptr<LinkSystem> link_system,
                   std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
                   const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                       buffer_collection_importers)
    : dispatcher_holder_(std::move(dispatcher_holder)),
      binding_(this, std::move(request), dispatcher_holder_->dispatcher()),
      session_id_(session_id),
      destroy_instance_function_(std::move(destroy_instance_function)),
      peer_closed_waiter_(binding_.channel().get(), ZX_CHANNEL_PEER_CLOSED),
      present2_helper_([this](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        if (binding_.is_bound()) {
          binding_.events().OnFramePresented(std::move(info));
        }
      }),
      flatland_presenter_(std::move(flatland_presenter)),
      link_system_(std::move(link_system)),
      uber_struct_queue_(std::move(uber_struct_queue)),
      buffer_collection_importers_(buffer_collection_importers),
      transform_graph_(session_id_),
      local_root_(transform_graph_.CreateTransform()),
      error_reporter_(scenic_impl::ErrorReporter::Default()) {
  zx_status_t status = peer_closed_waiter_.Begin(
      dispatcher(),
      [this](async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
             const zx_packet_signal_t* signal) { destroy_instance_function_(); });
  FX_DCHECK(status == ZX_OK);
}

Flatland::~Flatland() {
  // TODO(fxbug.dev/55374): consider if Link tokens should be returned or not.
}

void Flatland::Present(fuchsia::ui::composition::PresentArgs args) {
  TRACE_DURATION("gfx", "Flatland::Present");
  TRACE_FLOW_END("gfx", "Flatland::Present", present_count_);
  ++present_count_;
  // Close any clients that had invalid operations on link protocols.
  if (link_protocol_error_) {
    CloseConnection(FlatlandError::BAD_HANGING_GET);
    return;
  }

  // Close any clients that call Present() without any present tokens.
  if (present_credits_ == 0) {
    CloseConnection(FlatlandError::NO_PRESENTS_REMAINING);
    return;
  }
  present_credits_--;

  // If any fields are missing, replace them with the default values.
  if (!args.has_requested_presentation_time()) {
    args.set_requested_presentation_time(0);
  }
  if (!args.has_release_fences()) {
    args.set_release_fences({});
  }
  if (!args.has_acquire_fences()) {
    args.set_acquire_fences({});
  }
  if (!args.has_unsquashable()) {
    args.set_unsquashable(false);
  }

  auto root_handle = GetRoot();

  // TODO(fxbug.dev/40818): Decide on a proper limit on compute time for topological sorting.
  auto data = transform_graph_.ComputeAndCleanup(root_handle, std::numeric_limits<uint64_t>::max());
  FX_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());

  // TODO(fxbug.dev/36166): Once the 2D scene graph is externalized, don't commit changes if a cycle
  // is detected. Instead, kill the channel and remove the sub-graph from the global graph.
  failure_since_previous_present_ |= !data.cyclical_edges.empty();

  if (failure_since_previous_present_) {
    CloseConnection(FlatlandError::BAD_OPERATION);
    return;
  }

  FX_DCHECK(data.sorted_transforms[0].handle == root_handle);

  // Cleanup released resources. Here we also collect the list of unused images so they can be
  // released by the buffer collection importers.
  std::vector<allocation::ImageMetadata> images_to_release;
  for (const auto& dead_handle : data.dead_transforms) {
    matrices_.erase(dead_handle);

    auto image_kv = image_metadatas_.find(dead_handle);
    if (image_kv != image_metadatas_.end()) {
      images_to_release.push_back(image_kv->second);
      image_metadatas_.erase(image_kv);
    }
  }

  // If there are images ready for release, create a release fence for the current Present() and
  // delay release until that fence is reached to ensure that the images are no longer referenced
  // in any render data.
  if (!images_to_release.empty()) {
    // Create a release fence specifically for the images.
    zx::event image_release_fence;
    zx_status_t status = zx::event::create(0, &image_release_fence);
    FX_DCHECK(status == ZX_OK);

    // Use a self-referencing async::WaitOnce to perform ImageImporter deregistration.
    // This is primarily so the handler does not have to live in the Flatland instance, which may
    // be destroyed before the release fence is signaled. WaitOnce moves the handler to the stack
    // prior to invoking it, so it is safe for the handler to delete the WaitOnce on exit.
    // Specifically, we move the wait object into the lambda function via |copy_ref = wait| to
    // ensure that the wait object lives. The callback will not trigger without this.
    auto wait = std::make_shared<async::WaitOnce>(image_release_fence.get(), ZX_EVENT_SIGNALED);
    status = wait->Begin(
        dispatcher(), [copy_ref = wait, importer_ref = buffer_collection_importers_,
                       images_to_release](async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                          const zx_packet_signal_t* /*signal*/) mutable {
          FX_DCHECK(status == ZX_OK);

          for (auto& image_id : images_to_release) {
            for (auto& importer : importer_ref) {
              importer->ReleaseBufferImage(image_id.identifier);
            }
          }
        });
    FX_DCHECK(status == ZX_OK);

    // Push the new release fence into the user-provided list.
    args.mutable_release_fences()->push_back(std::move(image_release_fence));
  }

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->local_topology = std::move(data.sorted_transforms);

  for (const auto& [link_id, child_link] : child_links_) {
    ViewportProperties initial_properties;
    fidl::Clone(child_link.properties, &initial_properties);
    uber_struct->link_properties[child_link.link.parent_viewport_watcher_handle] =
        std::move(initial_properties);
  }

  for (const auto& [handle, matrix_data] : matrices_) {
    uber_struct->local_matrices[handle] = matrix_data.GetMatrix();
  }

  for (const auto& [handle, sample_region] : image_sample_regions_) {
    uber_struct->local_image_sample_regions[handle] = sample_region;
  }

  for (const auto& [handle, opacity_value] : opacity_values_) {
    uber_struct->local_opacity_values[handle] = opacity_value;
  }

  uber_struct->images = image_metadatas_;

  // Register a Present to get the PresentId needed to queue the UberStruct. This happens before
  // waiting on the acquire fences to indicate that a Present is pending.
  auto present_id =
      flatland_presenter_->RegisterPresent(session_id_, std::move(*args.mutable_release_fences()));
  present2_helper_.RegisterPresent(present_id,
                                   /*present_received_time=*/zx::time(async_now(dispatcher())));

  TRACE_FLOW_BEGIN("gfx", "ScheduleUpdate", present_id);

  // Safe to capture |this| because the Flatland is guaranteed to outlive |fence_queue_|,
  // Flatland is non-movable and FenceQueue does not fire closures after destruction.
  // TODO(fxbug.dev/76640): make the fences be the first arg, and the closure be the second.
  fence_queue_->QueueTask(
      [this, present_id, requested_presentation_time = args.requested_presentation_time(),
       unsquashable = args.unsquashable(), uber_struct = std::move(uber_struct),
       link_operations = std::move(pending_link_operations_),
       release_fences = std::move(*args.mutable_release_fences())]() mutable {
        // Push the UberStruct, then schedule the associated Present that will eventually publish
        // it to the InstanceMap used for rendering.
        uber_struct_queue_->Push(present_id, std::move(uber_struct));
        flatland_presenter_->ScheduleUpdateForSession(zx::time(requested_presentation_time),
                                                      {session_id_, present_id}, unsquashable);

        // Finalize Link destruction operations after publishing the new UberStruct. This
        // ensures that any local Transforms referenced by the to-be-deleted Links are already
        // removed from the now-published UberStruct.
        for (auto& operation : link_operations) {
          operation();
        }
      },
      std::move(*args.mutable_acquire_fences()));

  // We exited early in this method if there was a failure, and none of the subsequent operations
  // are allowed to trigger a failure (all failure possibilities should be checked before the
  // early exit).
  FX_DCHECK(!failure_since_previous_present_);
}

void Flatland::CreateView(ViewCreationToken token,
                          fidl::InterfaceRequest<ParentViewportWatcher> parent_viewport_watcher) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    error_reporter_->ERROR() << "CreateView failed, ViewCreationToken was invalid";
    ReportBadOperationError();
    return;
  }

  FX_DCHECK(link_system_);

  // This portion of the method is not feed forward. This makes it possible for clients to receive
  // layout information before this operation has been presented. By initializing the link
  // immediately, parents can inform children of layout changes, and child clients can perform
  // layout decisions before their first call to Present().
  auto link_origin = transform_graph_.CreateTransform();
  LinkSystem::ParentLink link = link_system_->CreateParentLink(
      dispatcher_holder_, std::move(token), std::move(parent_viewport_watcher), link_origin,
      [ref = weak_from_this(),
       dispatcher_holder = dispatcher_holder_](const std::string& error_log) {
        FX_CHECK(dispatcher_holder->dispatcher() == async_get_default_dispatcher())
            << "Link protocol error reported on the wrong dispatcher.";
        if (auto impl = ref.lock())
          impl->ReportLinkProtocolError(error_log);
      });

  // This portion of the method is feed-forward. The parent-child relationship between
  // |link_origin| and |local_root_| establishes the Transform hierarchy between the two instances,
  // but the operation will not be visible until the next Present() call includes that topology.
  if (parent_link_.has_value()) {
    bool child_removed =
        transform_graph_.RemoveChild(parent_link_->child_view_watcher_handle, local_root_);
    FX_DCHECK(child_removed);

    bool transform_released =
        transform_graph_.ReleaseTransform(parent_link_->child_view_watcher_handle);
    FX_DCHECK(transform_released);

    // Delay the destruction of the previous parent link until the next Present().
    pending_link_operations_.push_back(
        [local_link = std::move(parent_link_)]() mutable { local_link.reset(); });
  }

  bool child_added = transform_graph_.AddChild(link.child_view_watcher_handle, local_root_);
  FX_DCHECK(child_added);
  parent_link_ = std::move(link);
}

void Flatland::CreateView2(ViewCreationToken token,
                           fuchsia::ui::views::ViewIdentityOnCreation view_identity,
                           fuchsia::ui::composition::ViewBoundProtocols protocols,
                           fidl::InterfaceRequest<ParentViewportWatcher> parent_viewport_watcher) {
  // TODO(fxb/81795): Implement viewref and protocol hookup.
  CreateView(std::move(token), std::move(parent_viewport_watcher));
}

void Flatland::ReleaseView(std::function<void(fuchsia::ui::views::ViewCreationToken)> callback) {
  if (!parent_link_) {
    error_reporter_->ERROR() << "ReleaseView failed, no existing parent Link";
    ReportBadOperationError();
    return;
  }

  // Deleting the old ParentLink's Transform effectively changes this intance's root back to
  // |local_root_|.
  bool child_removed =
      transform_graph_.RemoveChild(parent_link_->child_view_watcher_handle, local_root_);
  FX_DCHECK(child_removed);

  bool transform_released =
      transform_graph_.ReleaseTransform(parent_link_->child_view_watcher_handle);
  FX_DCHECK(transform_released);

  // Move the old parent link into the delayed operation so that it isn't taken into account when
  // computing the local topology, but doesn't get deleted until after the new UberStruct is
  // published.
  auto local_link = std::move(parent_link_.value());
  parent_link_.reset();

  // Delay the actual destruction of the Link until the next Present().
  pending_link_operations_.push_back(
      [local_link = std::move(local_link), callback = std::move(callback)]() mutable {
        ViewCreationToken return_token;

        // If the link is still valid, return the original token. If not, create an orphaned
        // zx::channel and return it since the ObjectLinker does not retain the orphaned token.
        auto link_token = local_link.exporter.ReleaseToken();
        if (link_token.has_value()) {
          return_token.value = zx::channel(std::move(link_token.value()));
        } else {
          // |peer_token| immediately falls out of scope, orphaning |return_token|.
          zx::channel peer_token;
          zx::channel::create(0, &return_token.value, &peer_token);
        }

        callback(std::move(return_token));
      });
}

void Flatland::Clear() {
  // Clear user-defined mappings and local matrices.
  transforms_.clear();
  content_handles_.clear();
  matrices_.clear();

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
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "CreateTransform called with transform_id 0";
    ReportBadOperationError();
    return;
  }

  if (transforms_.count(transform_id.value)) {
    error_reporter_->ERROR() << "CreateTransform called with pre-existing transform_id "
                             << transform_id.value;
    ReportBadOperationError();
    return;
  }

  TransformHandle handle = transform_graph_.CreateTransform();
  transforms_.insert({transform_id.value, handle});
}

void Flatland::SetTranslation(TransformId transform_id, Vec translation) {
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetTranslation called with transform_id 0";
    ReportBadOperationError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id.value);

  if (transform_kv == transforms_.end()) {
    error_reporter_->ERROR() << "SetTranslation failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  matrices_[transform_kv->second].SetTranslation(translation);
}

void Flatland::SetOrientation(TransformId transform_id, Orientation orientation) {
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetOrientation called with transform_id 0";
    ReportBadOperationError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id.value);

  if (transform_kv == transforms_.end()) {
    error_reporter_->ERROR() << "SetOrientation failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  matrices_[transform_kv->second].SetOrientation(orientation);
}

void Flatland::AddChild(TransformId parent_transform_id, TransformId child_transform_id) {
  if (parent_transform_id.value == kInvalidId || child_transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "AddChild called with transform_id zero";
    ReportBadOperationError();
    return;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id.value);
  auto child_global_kv = transforms_.find(child_transform_id.value);

  if (parent_global_kv == transforms_.end()) {
    error_reporter_->ERROR() << "AddChild failed, parent_transform_id " << parent_transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  if (child_global_kv == transforms_.end()) {
    error_reporter_->ERROR() << "AddChild failed, child_transform_id " << child_transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  if (opacity_values_.find(parent_global_kv->second) != opacity_values_.end() &&
      opacity_values_[parent_global_kv->second] != 1.f) {
    error_reporter_->ERROR() << "Cannot add a child to a node with an opacity value < 1.0.";
    ReportBadOperationError();
    return;
  }

  bool added = transform_graph_.AddChild(parent_global_kv->second, child_global_kv->second);

  if (!added) {
    error_reporter_->ERROR() << "AddChild failed, connection already exists between parent "
                             << parent_transform_id.value << " and child "
                             << child_transform_id.value;
    ReportBadOperationError();
  }
}

void Flatland::RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) {
  if (parent_transform_id.value == kInvalidId || child_transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "RemoveChild failed, transform_id " << parent_transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  auto parent_global_kv = transforms_.find(parent_transform_id.value);
  auto child_global_kv = transforms_.find(child_transform_id.value);

  if (parent_global_kv == transforms_.end()) {
    error_reporter_->ERROR() << "RemoveChild failed, parent_transform_id "
                             << parent_transform_id.value << " not found";
    ReportBadOperationError();
    return;
  }

  if (child_global_kv == transforms_.end()) {
    error_reporter_->ERROR() << "RemoveChild failed, child_transform_id "
                             << child_transform_id.value << " not found";
    ReportBadOperationError();
    return;
  }

  bool removed = transform_graph_.RemoveChild(parent_global_kv->second, child_global_kv->second);

  if (!removed) {
    error_reporter_->ERROR() << "RemoveChild failed, connection between parent "
                             << parent_transform_id.value << " and child "
                             << child_transform_id.value << " not found";
    ReportBadOperationError();
  }
}

void Flatland::SetRootTransform(TransformId transform_id) {
  // SetRootTransform(0) is special -- it only clears the existing root transform.
  if (transform_id.value == kInvalidId) {
    transform_graph_.ClearChildren(local_root_);
    return;
  }

  auto global_kv = transforms_.find(transform_id.value);
  if (global_kv == transforms_.end()) {
    error_reporter_->ERROR() << "SetRootTransform failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  transform_graph_.ClearChildren(local_root_);

  bool added = transform_graph_.AddChild(local_root_, global_kv->second);
  FX_DCHECK(added);
}

void Flatland::CreateViewport(ContentId link_id, ViewportCreationToken token,
                              ViewportProperties properties,
                              fidl::InterfaceRequest<ChildViewWatcher> child_view_watcher) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    error_reporter_->ERROR() << "CreateViewport failed, ViewportCreationToken was invalid";
    ReportBadOperationError();
    return;
  }

  if (!properties.has_logical_size()) {
    error_reporter_->ERROR()
        << "CreateViewport must be provided a ViewportProperties with a logical size";
    ReportBadOperationError();
    return;
  }

  auto logical_size = properties.logical_size();
  if (logical_size.width == 0 || logical_size.height == 0) {
    error_reporter_->ERROR()
        << "CreateViewport must be provided a logical size with positive width and height values";
    ReportBadOperationError();
    return;
  }

  FX_DCHECK(link_system_);

  // The ViewportProperties and ChildViewWatcherImpl live on a handle from this Flatland instance.
  auto parent_viewport_watcher_handle = transform_graph_.CreateTransform();

  // We can initialize the Link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method. We also forward the initial ViewportProperties through
  // the LinkSystem immediately, so the child can receive them as soon as possible.
  ViewportProperties initial_properties;
  fidl::Clone(properties, &initial_properties);
  LinkSystem::ChildLink link = link_system_->CreateChildLink(
      dispatcher_holder_, std::move(token), std::move(initial_properties),
      std::move(child_view_watcher), parent_viewport_watcher_handle,
      [ref = weak_from_this(),
       dispatcher_holder = dispatcher_holder_](const std::string& error_log) {
        FX_CHECK(dispatcher_holder->dispatcher() == async_get_default_dispatcher())
            << "Link protocol error reported on the wrong dispatcher.";
        if (auto impl = ref.lock())
          impl->ReportLinkProtocolError(error_log);
      });

  // TODO(fxbug.dev/76640): probably move this up before creating the child-link.
  if (link_id.value == kInvalidId) {
    error_reporter_->ERROR() << "CreateViewport called with ContentId zero";
    ReportBadOperationError();
    return;
  }

  // TODO(fxbug.dev/76640): probably move this up before creating the child-link.
  if (content_handles_.count(link_id.value)) {
    error_reporter_->ERROR() << "CreateViewport called with existing ContentId " << link_id.value;
    ReportBadOperationError();
    return;
  }

  // This is the feed-forward portion of the method. Here, we add the link to the map, and
  // initialize its layout with the desired properties. The Link will not actually result in
  // additions to the Transform hierarchy until it is added to a Transform.
  bool child_added =
      transform_graph_.AddChild(link.parent_viewport_watcher_handle, link.link_handle);
  FX_DCHECK(child_added);

  // Default the link size to the logical size, which is just an identity scale matrix, so
  // that future logical size changes will result in the correct scale matrix.
  SizeU size = properties.logical_size();

  content_handles_[link_id.value] = link.parent_viewport_watcher_handle;
  child_links_[link.parent_viewport_watcher_handle] = {
      .link = std::move(link), .properties = std::move(properties), .size = std::move(size)};
}

void Flatland::CreateImage(ContentId image_id,
                           fuchsia::ui::composition::BufferCollectionImportToken import_token,
                           uint32_t vmo_index, ImageProperties properties) {
  if (image_id.value == kInvalidId) {
    error_reporter_->ERROR() << "CreateImage called with image_id 0";
    ReportBadOperationError();
    return;
  }

  if (content_handles_.count(image_id.value)) {
    error_reporter_->ERROR() << "CreateImage called with pre-existing image_id " << image_id.value;
    ReportBadOperationError();
    return;
  }

  const BufferCollectionId global_collection_id = fsl::GetRelatedKoid(import_token.value.get());

  // Check if there is a valid peer.
  if (global_collection_id == ZX_KOID_INVALID) {
    error_reporter_->ERROR() << "CreateImage called with no valid export token";
    ReportBadOperationError();
    return;
  }

  if (!properties.has_size()) {
    error_reporter_->ERROR() << "CreateImage failed, ImageProperties did not specify size";
    ReportBadOperationError();
    return;
  }

  if (!properties.size().width) {
    error_reporter_->ERROR() << "CreateImage failed, ImageProperties did not specify a width";
    ReportBadOperationError();
    return;
  }

  if (!properties.size().height) {
    error_reporter_->ERROR() << "CreateImage failed, ImageProperties did not specify a height";
    ReportBadOperationError();
    return;
  }

  allocation::ImageMetadata metadata;
  metadata.identifier = allocation::GenerateUniqueImageId();
  metadata.collection_id = global_collection_id;
  metadata.vmo_index = vmo_index;
  metadata.width = properties.size().width;
  metadata.height = properties.size().height;
  metadata.is_opaque = false;

  for (uint32_t i = 0; i < buffer_collection_importers_.size(); i++) {
    auto& importer = buffer_collection_importers_[i];

    // TODO(fxbug.dev/62240): Give more detailed errors.
    auto result = importer->ImportBufferImage(metadata);
    if (!result) {
      // If this importer fails, we need to release the image from
      // all of the importers that it passed on. Luckily we can do
      // this right here instead of waiting for a fence since we know
      // this image isn't being used by anything yet.
      for (uint32_t j = 0; j < i; j++) {
        buffer_collection_importers_[j]->ReleaseBufferImage(metadata.identifier);
      }

      error_reporter_->ERROR() << "Importer could not import image.";
      ReportBadOperationError();
      return;
    }
  }

  // Now that we've successfully been able to import the image into the importers,
  // we can now create a handle for it in the transform graph, and add the metadata
  // to our map.
  auto handle = transform_graph_.CreateTransform();
  content_handles_[image_id.value] = handle;
  image_metadatas_[handle] = metadata;

  // Set the default sample region of the image to be the full image.
  SetImageSampleRegion(image_id, {0, 0, static_cast<float>(properties.size().width),
                                  static_cast<float>(properties.size().height)});
}

void Flatland::SetImageSampleRegion(ContentId image_id, RectF rect) {
  if (image_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetImageSampleRegion called with content id 0";
    ReportBadOperationError();
    return;
  }

  auto content_kv = content_handles_.find(image_id.value);
  if (content_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "SetImageSampleRegion called with non-existent image_id "
                             << image_id.value;
    ReportBadOperationError();
    return;
  }

  auto image_kv = image_metadatas_.find(content_kv->second);
  if (image_kv == image_metadatas_.end()) {
    error_reporter_->ERROR() << "SetImageSampleRegion called on non-image content.";
    ReportBadOperationError();
    return;
  }

  // The provided sample region needs to be within the bounds of the image.
  auto metadata = image_kv->second;
  if (rect.x < 0.f || rect.x > metadata.width || rect.width < 0.f ||
      (rect.x + rect.width) > metadata.width || rect.y < 0.f || rect.y > metadata.height ||
      rect.height < 0.f || (rect.y + rect.height) > metadata.height) {
    error_reporter_->ERROR() << "SetImageSampleRegion rect out of bounds for image.";
    ReportBadOperationError();
    return;
  }

  image_sample_regions_[content_kv->second] = rect;
}

void Flatland::SetImageDestinationSize(ContentId image_id, SizeU size) {
  if (image_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetImageSize called with image_id 0";
    ReportBadOperationError();
    return;
  }

  auto content_kv = content_handles_.find(image_id.value);

  if (content_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "SetImageSize called with non-existent image_id " << image_id.value;
    ReportBadOperationError();
    return;
  }

  auto image_kv = image_metadatas_.find(content_kv->second);
  if (image_kv == image_metadatas_.end()) {
    error_reporter_->ERROR() << "SetImageSize called on non-image content  " << image_id.value;
    ReportBadOperationError();
    return;
  }

  // TODO(fxbug.dev/77993): Remove matrices from flatland and make this a vec.
  matrices_[content_kv->second].SetScale(size);
}

void Flatland::SetOpacity(TransformId transform_id, float val) {
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetOpacity called with transform_id 0";
    ReportBadOperationError();
    return;
  }

  if (val < 0.f || val > 1.f) {
    error_reporter_->ERROR() << "Opacity value is not within valid range [0,1].";
    ReportBadOperationError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id.value);

  if (transform_kv == transforms_.end()) {
    error_reporter_->ERROR() << "SetOpacity failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  if (transform_graph_.HasChildren(transform_kv->second)) {
    error_reporter_->ERROR() << "Cannot set the opacity value of a non-leaf node below 1.0";
    ReportBadOperationError();
    return;
  }

  // Erase the value from the map since we store 1.f implicity.
  if (val == 1.f) {
    opacity_values_.erase(transform_kv->second);
  } else {
    opacity_values_[transform_kv->second] = val;
  }
}

void Flatland::SetContent(TransformId transform_id, ContentId content_id) {
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetContent called with transform_id zero";
    ReportBadOperationError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id.value);

  if (transform_kv == transforms_.end()) {
    error_reporter_->ERROR() << "SetContent failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  if (content_id.value == kInvalidId) {
    transform_graph_.ClearPriorityChild(transform_kv->second);
    return;
  }

  auto handle_kv = content_handles_.find(content_id.value);

  if (handle_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "SetContent failed, content_id " << content_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  transform_graph_.SetPriorityChild(transform_kv->second, handle_kv->second);
}

void Flatland::SetViewportProperties(ContentId link_id, ViewportProperties properties) {
  if (link_id.value == kInvalidId) {
    error_reporter_->ERROR() << "SetViewportProperties called with link_id zero.";
    ReportBadOperationError();
    return;
  }

  auto content_kv = content_handles_.find(link_id.value);

  if (content_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "SetViewportProperties failed, link_id " << link_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  auto link_kv = child_links_.find(content_kv->second);

  if (link_kv == child_links_.end()) {
    error_reporter_->ERROR() << "SetViewportProperties failed, content_id " << link_id.value
                             << " is not a Link";
    ReportBadOperationError();
    return;
  }

  // Callers do not have to provide a new logical size on every call to SetViewportProperties, but
  // if they do, it must have positive width and height values.
  if (properties.has_logical_size()) {
    auto logical_size = properties.logical_size();
    if (logical_size.width == 0 || logical_size.height == 0) {
      error_reporter_->ERROR()
          << "SetViewportProperties failed, logical_size components must be positive, "
          << "given (" << logical_size.width << ", " << logical_size.height << ")";
      ReportBadOperationError();
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
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  if (transform_id.value == kInvalidId) {
    error_reporter_->ERROR() << "ReleaseTransform called with transform_id zero";
    ReportBadOperationError();
    return;
  }

  auto transform_kv = transforms_.find(transform_id.value);

  if (transform_kv == transforms_.end()) {
    error_reporter_->ERROR() << "ReleaseTransform failed, transform_id " << transform_id.value
                             << " not found";
    ReportBadOperationError();
    return;
  }

  bool erased_from_graph = transform_graph_.ReleaseTransform(transform_kv->second);
  FX_DCHECK(erased_from_graph);
  transforms_.erase(transform_kv);
}

void Flatland::ReleaseViewport(
    ContentId link_id, fuchsia::ui::composition::Flatland::ReleaseViewportCallback callback) {
  if (link_id.value == kInvalidId) {
    error_reporter_->ERROR() << "ReleaseViewport called with link_id zero";
    ReportBadOperationError();
    return;
  }

  auto content_kv = content_handles_.find(link_id.value);

  if (content_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "ReleaseViewport failed, link_id " << link_id.value << " not found";
    ReportBadOperationError();
    return;
  }

  auto link_kv = child_links_.find(content_kv->second);

  if (link_kv == child_links_.end()) {
    error_reporter_->ERROR() << "ReleaseViewport failed, content_id " << link_id.value
                             << " is not a Link";
    ReportBadOperationError();
    return;
  }

  // Deleting the ChildLink's |parent_viewport_watcher_handle| effectively deletes the link from the
  // local topology, even if the link object itself is not deleted.
  bool child_removed = transform_graph_.RemoveChild(
      link_kv->second.link.parent_viewport_watcher_handle, link_kv->second.link.link_handle);
  FX_DCHECK(child_removed);

  bool content_released =
      transform_graph_.ReleaseTransform(link_kv->second.link.parent_viewport_watcher_handle);
  FX_DCHECK(content_released);

  // Move the old child link into the delayed operation so that the ContentId is immeditely free
  // for re-use, but it doesn't get deleted until after the new UberStruct is published.
  auto child_link = std::move(link_kv->second);
  child_links_.erase(content_kv->second);
  content_handles_.erase(content_kv);

  // Delay the actual destruction of the link until the next Present().
  pending_link_operations_.push_back(
      [child_link = std::move(child_link), callback = std::move(callback)]() mutable {
        ViewportCreationToken return_token;

        // If the link is still valid, return the original token. If not, create an orphaned
        // zx::channel and return it since the ObjectLinker does not retain the orphaned token.
        auto link_token = child_link.link.importer.ReleaseToken();
        if (link_token.has_value()) {
          return_token.value = zx::channel(std::move(link_token.value()));
        } else {
          // |peer_token| immediately falls out of scope, orphaning |return_token|.
          zx::channel peer_token;
          zx::channel::create(0, &return_token.value, &peer_token);
        }

        callback(std::move(return_token));
      });
}

void Flatland::ReleaseImage(ContentId image_id) {
  if (image_id.value == kInvalidId) {
    error_reporter_->ERROR() << "ReleaseImage called with image_id zero";
    ReportBadOperationError();
    return;
  }

  auto content_kv = content_handles_.find(image_id.value);

  if (content_kv == content_handles_.end()) {
    error_reporter_->ERROR() << "ReleaseImage failed, image_id " << image_id.value << " not found";
    ReportBadOperationError();
    return;
  }

  auto image_kv = image_metadatas_.find(content_kv->second);

  if (image_kv == image_metadatas_.end()) {
    error_reporter_->ERROR() << "ReleaseImage failed, content_id " << image_id.value
                             << " is not an Image";
    ReportBadOperationError();
    return;
  }

  bool erased_from_graph = transform_graph_.ReleaseTransform(content_kv->second);
  FX_DCHECK(erased_from_graph);

  // Even though the handle is released, it may still be referenced by client Transforms. The
  // image_metadatas_ map preserves the entry until it shows up in the dead_transforms list.
  content_handles_.erase(image_id.value);
}

void Flatland::SetDebugName(std::string name) {
  std::stringstream stream;
  if (!name.empty())
    stream << "Flatland client(" << name << "): ";
  error_reporter_->SetPrefix(stream.str());
}

void Flatland::OnNextFrameBegin(uint32_t additional_present_credits,
                                FuturePresentationInfos presentation_infos) {
  TRACE_DURATION("gfx", "Flatland::OnNextFrameBegin");
  present_credits_ += additional_present_credits;

  // Only send an `OnNextFrameBegin` event if the client has at least one present credit. It is
  // guaranteed that this won't stall clients because the current policy is to always return
  // present tokens upon processing them. If and when a new policy is adopted, we should take care
  // to ensure this guarantee is upheld.
  if (present_credits_ > 0 && binding_.is_bound()) {
    OnNextFrameBeginValues values;
    values.set_additional_present_credits(additional_present_credits);
    values.set_future_presentation_infos(std::move(presentation_infos));

    binding_.events().OnNextFrameBegin(std::move(values));
  }
}

void Flatland::OnFramePresented(const std::map<scheduling::PresentId, zx::time>& latched_times,
                                scheduling::PresentTimestamps present_times) {
  TRACE_DURATION("gfx", "Flatland::OnFramePresented");
  // TODO(fxbug.dev/63305): remove `num_presents_allowed` from this event.  Clients should obtain
  // this information from OnPresentProcessedValues().
  present2_helper_.OnPresented(latched_times, present_times, /*num_presents_allowed=*/0);
}

TransformHandle Flatland::GetRoot() const {
  return parent_link_ ? parent_link_->child_view_watcher_handle : local_root_;
}

std::optional<TransformHandle> Flatland::GetContentHandle(ContentId content_id) const {
  auto handle_kv = content_handles_.find(content_id.value);
  if (handle_kv == content_handles_.end()) {
    return std::nullopt;
  }
  return handle_kv->second;
}

void Flatland::SetErrorReporter(std::shared_ptr<scenic_impl::ErrorReporter> error_reporter) {
  error_reporter_ = error_reporter;
}

scheduling::SessionId Flatland::GetSessionId() const { return session_id_; }

void Flatland::ReportBadOperationError() { failure_since_previous_present_ = true; }

void Flatland::ReportLinkProtocolError(const std::string& error_log) {
  error_reporter_->ERROR() << error_log;
  link_protocol_error_ = true;
}

void Flatland::CloseConnection(FlatlandError error) {
  // Send the error to the client before closing the connection.
  binding_.events().OnError(error);

  // Cancel the async::Wait before closing the connection, or it will assert on destruction.
  zx_status_t status = peer_closed_waiter_.Cancel();

  // Immediately close the FIDL interface to prevent future requests.
  binding_.Close(ZX_ERR_BAD_STATE);

  // Finally, trigger the destruction of this instance.
  destroy_instance_function_();
}

// MatrixData function implementations

// static
float Flatland::MatrixData::GetOrientationAngle(fuchsia::ui::composition::Orientation orientation) {
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

void Flatland::MatrixData::SetTranslation(Vec translation) {
  // TODO(fxbug.dev/77993): Remove these casts once we remove the floating
  // point matrices and replace with integer vectors.
  translation_.x = static_cast<float>(translation.x);
  translation_.y = static_cast<float>(translation.y);
  RecomputeMatrix();
}

void Flatland::MatrixData::SetOrientation(fuchsia::ui::composition::Orientation orientation) {
  angle_ = GetOrientationAngle(orientation);

  RecomputeMatrix();
}

void Flatland::MatrixData::SetScale(SizeU scale) {
  scale_.x = static_cast<float>(scale.width);
  scale_.y = static_cast<float>(scale.height);

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
