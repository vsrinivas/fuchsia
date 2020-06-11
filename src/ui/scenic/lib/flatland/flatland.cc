// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

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

Flatland::Flatland(const std::shared_ptr<Renderer>& renderer,
                   const std::shared_ptr<LinkSystem>& link_system,
                   const std::shared_ptr<UberStructSystem>& uber_struct_system,
                   fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
    : renderer_(renderer),
      link_system_(link_system),
      uber_struct_system_(uber_struct_system),
      sysmem_allocator_(std::move(sysmem_allocator)),
      instance_id_(uber_struct_system_->GetNextInstanceId()),
      transform_graph_(instance_id_),
      local_root_(transform_graph_.CreateTransform()) {}

Flatland::~Flatland() { uber_struct_system_->ClearUberStruct(instance_id_); }

void Flatland::Present(PresentCallback callback) {
  bool success = true;

  // TODO(36161): Don't execute operations until the (yet to be added) acquire fences have been
  // reached.
  for (auto& operation : pending_operations_) {
    if (!operation()) {
      success = false;
      break;
    }
  }

  pending_operations_.clear();

  auto root_handle = GetRoot();

  // TODO(40818): Decide on a proper limit on compute time for topological sorting.
  auto data = transform_graph_.ComputeAndCleanup(root_handle, std::numeric_limits<uint64_t>::max());
  FX_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());

  // TODO(36166): Once the 2D scene graph is externalized, don't commit changes if a cycle is
  // detected. Instead, kill the channel and remove the sub-graph from the global graph.
  success &= data.cyclical_edges.empty();

  if (success) {
    FX_DCHECK(data.sorted_transforms[0].handle == root_handle);

    // Cleanup released resources.
    for (const auto& dead_handle : data.dead_transforms) {
      matrices_.erase(dead_handle);
      // TODO(52052): clean up the Renderer's image resources as well.
      image_metadatas_.erase(dead_handle);
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

    uber_struct_system_->SetUberStruct(instance_id_, std::move(uber_struct));
    // TODO(36161): Once present operations can be pipelined, this variable will change state based
    // on the number of outstanding Present calls. Until then, this call is synchronous, and we can
    // always return 1 as the number of remaining presents.
    callback(fit::ok(num_presents_remaining_));
  } else {
    callback(fit::error(Error::BAD_OPERATION));
  }
}

void Flatland::LinkToParent(GraphLinkToken token, fidl::InterfaceRequest<GraphLink> graph_link) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    pending_operations_.push_back([]() {
      FX_LOGS(ERROR) << "LinkToParent failed, GraphLinkToken was invalid";
      return false;
    });
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

  // This portion of the method is feed-forward. Our Link should not actually be changed until
  // Present() is called, so that the update to the Link is atomic with all other operations in the
  // batch. The parent-child relationship between |link_origin| and |local_root_| establishes the
  // transform hierarchy between the two instances.
  pending_operations_.push_back([this, link = std::move(link)]() mutable {
    if (parent_link_) {
      bool child_removed = transform_graph_.RemoveChild(parent_link_->link_origin, local_root_);
      FX_DCHECK(child_removed);

      bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
      FX_DCHECK(transform_released);
    }
    bool child_added = transform_graph_.AddChild(link.link_origin, local_root_);
    FX_DCHECK(child_added);
    parent_link_ = std::move(link);
    return true;
  });
}

void Flatland::UnlinkFromParent(
    fuchsia::ui::scenic::internal::Flatland::UnlinkFromParentCallback callback) {
  pending_operations_.push_back([this, callback = std::move(callback)]() {
    if (!parent_link_) {
      FX_LOGS(ERROR) << "UnlinkFromParent failed, no existing parent link";
      return false;
    }

    GraphLinkToken return_token;

    // If the link is still valid, return the original token. If not, create an orphaned
    // zx::eventpair and return it since the ObjectLinker does not retain the orphaned token.
    auto link_token = parent_link_->exporter.ReleaseToken();
    if (link_token.has_value()) {
      return_token.value = zx::eventpair(std::move(link_token.value()));
    } else {
      // |peer_token| immediately falls out of scope, orphaning |return_token|.
      zx::eventpair peer_token;
      zx::eventpair::create(0, &return_token.value, &peer_token);
    }

    bool child_removed = transform_graph_.RemoveChild(parent_link_->link_origin, local_root_);
    FX_DCHECK(child_removed);

    bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
    FX_DCHECK(transform_released);

    parent_link_.reset();

    callback(std::move(return_token));

    return true;
  });
}

void Flatland::ClearGraph() {
  pending_operations_.push_back([=]() {
    transforms_.clear();
    // We always preserve the link origin when clearing the graph.
    transform_graph_.ResetGraph(local_root_);
    child_links_.clear();
    parent_link_.reset();
    return true;
  });
}

void Flatland::CreateTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "CreateTransform called with transform_id 0";
      return false;
    }

    if (transforms_.count(transform_id)) {
      FX_LOGS(ERROR) << "CreateTransform called with pre-existing transform_id " << transform_id;
      return false;
    }

    TransformHandle handle = transform_graph_.CreateTransform();
    transforms_.insert({transform_id, handle});

    return true;
  });
}

void Flatland::SetTranslation(TransformId transform_id, Vec2 translation) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "SetTranslation called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "SetTranslation failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetTranslation(translation);

    return true;
  });
}

void Flatland::SetOrientation(TransformId transform_id, Orientation orientation) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "SetOrientation called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "SetOrientation failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetOrientation(orientation);

    return true;
  });
}

void Flatland::SetScale(TransformId transform_id, Vec2 scale) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "SetScale called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "SetScale failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetScale(scale);

    return true;
  });
}

void Flatland::AddChild(TransformId parent_transform_id, TransformId child_transform_id) {
  pending_operations_.push_back([=]() {
    if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "AddChild called with transform_id zero";
      return false;
    }

    auto parent_global_kv = transforms_.find(parent_transform_id);
    auto child_global_kv = transforms_.find(child_transform_id);

    if (parent_global_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "AddChild failed, parent_transform_id " << parent_transform_id
                     << " not found";
      return false;
    }

    if (child_global_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "AddChild failed, child_transform_id " << child_transform_id
                     << " not found";
      return false;
    }

    bool added = transform_graph_.AddChild(parent_global_kv->second, child_global_kv->second);

    if (!added) {
      FX_LOGS(ERROR) << "AddChild failed, connection already exists between parent "
                     << parent_transform_id << " and child " << child_transform_id;
    }

    return added;
  });
}

void Flatland::RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) {
  pending_operations_.push_back([=]() {
    if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "RemoveChild failed, transform_id " << parent_transform_id << " not found";
      return false;
    }

    auto parent_global_kv = transforms_.find(parent_transform_id);
    auto child_global_kv = transforms_.find(child_transform_id);

    if (parent_global_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "RemoveChild failed, parent_transform_id " << parent_transform_id
                     << " not found";
      return false;
    }

    if (child_global_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "RemoveChild failed, child_transform_id " << child_transform_id
                     << " not found";
      return false;
    }

    bool removed = transform_graph_.RemoveChild(parent_global_kv->second, child_global_kv->second);

    if (!removed) {
      FX_LOGS(ERROR) << "RemoveChild failed, connection between parent " << parent_transform_id
                     << " and child " << child_transform_id << " not found";
    }

    return removed;
  });
}

void Flatland::SetRootTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    transform_graph_.ClearChildren(local_root_);

    // SetRootTransform(0) is special -- it only clears the existing root transform.
    if (transform_id == kInvalidId) {
      return true;
    }

    auto global_kv = transforms_.find(transform_id);
    if (global_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    bool added = transform_graph_.AddChild(local_root_, global_kv->second);
    FX_DCHECK(added);
    return true;
  });
}

void Flatland::CreateLink(ContentId link_id, ContentLinkToken token, LinkProperties properties,
                          fidl::InterfaceRequest<ContentLink> content_link) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    pending_operations_.push_back([]() {
      FX_LOGS(ERROR) << "CreateLink failed, ContentLinkToken was invalid";
      return false;
    });
    return;
  }

  if (!properties.has_logical_size()) {
    pending_operations_.push_back([]() {
      FX_LOGS(ERROR) << "CreateLink must be provided a LinkProperties with a logical size.";
      return false;
    });
    return;
  }

  auto logical_size = properties.logical_size();
  if (logical_size.x <= 0.f || logical_size.y <= 0.f) {
    pending_operations_.push_back([]() {
      FX_LOGS(ERROR) << "CreateLink must be provided a logical size with positive X and Y values.";
      return false;
    });
  }

  FX_DCHECK(link_system_);

  // The LinkProperties and ContentLinkImpl live on a handle from this Flatland instance.
  auto graph_handle = transform_graph_.CreateTransform();

  // We can initialize the link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method. We also forward the initial LinkProperties through
  // the LinkSystem immediately, so the child can receive them as soon as possible.
  LinkProperties initial_properties;
  fidl::Clone(properties, &initial_properties);
  LinkSystem::ChildLink link = link_system_->CreateChildLink(
      std::move(token), std::move(initial_properties), std::move(content_link), graph_handle);

  // This is the feed-forward portion of the method. Here, we add the link to the map, and
  // initialize its layout with the desired properties. The link will not actually result in
  // additions to the transform hierarchy until it is added to a Transform.
  pending_operations_.push_back(
      [=, link = std::move(link), properties = std::move(properties)]() mutable {
        if (link_id == 0) {
          FX_LOGS(ERROR) << "CreateLink called with ContentId zero.";
          return false;
        }

        if (content_handles_.count(link_id)) {
          FX_LOGS(ERROR) << "CreateLink called with existing ContentId " << link_id;
          return false;
        }

        bool child_added = transform_graph_.AddChild(link.graph_handle, link.link_handle);
        FX_DCHECK(child_added);

        // Default the link size to the logical size, which is just an identity scale matrix, so
        // that future logical size changes will result in the correct scale matrix.
        Vec2 size = properties.logical_size();

        content_handles_[link_id] = link.graph_handle;
        child_links_[link.graph_handle] = {
            .link = std::move(link), .properties = std::move(properties), .size = std::move(size)};

        return true;
      });
}

void Flatland::RegisterBufferCollection(
    BufferCollectionId collection_id,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(renderer_);

  if (collection_id == 0) {
    pending_operations_.push_back([=]() {
      FX_LOGS(ERROR) << "RegisterBufferCollection called with collection_id 0";
      return false;
    });
    return;
  }

  if (buffer_collections_.count(collection_id)) {
    pending_operations_.push_back([=]() {
      FX_LOGS(ERROR) << "RegisterBufferCollection called with pre-existing collection_id "
                     << collection_id;
      return false;
    });
    return;
  }

  // Register the texture collection immediately since the client may block on buffers being
  // allocated before calling Present().
  auto renderer_collection_id =
      renderer_->RegisterTextureCollection(sysmem_allocator_.get(), std::move(token));

  // But don't allow the collection to be referenced in other function calls until presented.
  pending_operations_.push_back([=]() {
    if (renderer_collection_id == Renderer::kInvalidId) {
      FX_LOGS(ERROR)
          << "RegisterBufferCollection failed to register the sysmem token with the renderer.";
      return false;
    }

    buffer_collections_[collection_id].collection_id = renderer_collection_id;

    return true;
  });
}

void Flatland::CreateImage(ContentId image_id, BufferCollectionId collection_id, uint32_t vmo_index,
                           ImageProperties properties) {
  FX_DCHECK(renderer_);

  pending_operations_.push_back([=, properties = std::move(properties)]() {
    if (image_id == 0) {
      FX_LOGS(ERROR) << "CreateImage called with image_id 0";
      return false;
    }

    if (content_handles_.count(image_id)) {
      FX_LOGS(ERROR) << "CreateImage called with pre-existing image_id " << image_id;
      return false;
    }

    auto buffer_kv = buffer_collections_.find(collection_id);

    if (buffer_kv == buffer_collections_.end()) {
      FX_LOGS(ERROR) << "CreateImage failed, collection_id " << collection_id << " not found.";
      return false;
    }

    auto& buffer_data = buffer_kv->second;

    // If the buffer hasn't been validated yet, try to validate it. If validation fails, it will be
    // impossible to render this image.
    if (!buffer_data.metadata.has_value()) {
      auto metadata = renderer_->Validate(buffer_data.collection_id);
      if (!metadata.has_value()) {
        FX_LOGS(ERROR) << "CreateImage failed, collection_id " << collection_id
                       << " has not been allocated yet.";
        return false;
      }

      buffer_data.metadata = std::move(metadata.value());
    }

    if (vmo_index >= buffer_data.metadata->vmo_count) {
      FX_LOGS(ERROR) << "CreateImage failed, vmo_index " << vmo_index
                     << " must be less than vmo_count " << buffer_data.metadata->vmo_count;
      return false;
    }

    const auto& image_constraints = buffer_data.metadata->image_constraints;

    if (!properties.has_width()) {
      FX_LOGS(ERROR) << "CreateImage failed, ImageProperties did not specify a width.";
      return false;
    }

    const uint32_t width = properties.width();
    if (width < image_constraints.min_coded_width || width > image_constraints.max_coded_width) {
      FX_LOGS(ERROR) << "CreateImage failed, width " << width << " is not within valid range ["
                     << image_constraints.min_coded_width << ","
                     << image_constraints.max_coded_width << "]";
      return false;
    }

    if (!properties.has_height()) {
      FX_LOGS(ERROR) << "CreateImage failed, ImageProperties did not specify a height.";
      return false;
    }

    const uint32_t height = properties.height();
    if (height < image_constraints.min_coded_height ||
        height > image_constraints.max_coded_height) {
      FX_LOGS(ERROR) << "CreateImage failed, height " << height << " is not within valid range ["
                     << image_constraints.min_coded_height << ","
                     << image_constraints.max_coded_height << "]";
      return false;
    }

    auto handle = transform_graph_.CreateTransform();
    content_handles_[image_id] = handle;

    auto& metadata = image_metadatas_[handle];
    metadata.collection_id = buffer_data.collection_id;
    metadata.vmo_idx = vmo_index;
    metadata.width = width;
    metadata.height = height;

    return true;
  });
}

void Flatland::SetContentOnTransform(ContentId content_id, TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "SetContentOnTransform called with transform_id zero";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "SetContentOnTransform failed, transform_id " << transform_id
                     << " not found";
      return false;
    }

    if (content_id == 0) {
      transform_graph_.ClearPriorityChild(transform_kv->second);
      return true;
    }

    auto handle_kv = content_handles_.find(content_id);

    if (handle_kv == content_handles_.end()) {
      FX_LOGS(ERROR) << "SetContentOnTransform failed, content_id " << content_id << " not found";
      return false;
    }

    transform_graph_.SetPriorityChild(transform_kv->second, handle_kv->second);
    return true;
  });
}

void Flatland::SetLinkProperties(ContentId link_id, LinkProperties properties) {
  pending_operations_.push_back([=, properties = std::move(properties)]() mutable {
    if (link_id == 0) {
      FX_LOGS(ERROR) << "SetLinkProperties called with link_id zero.";
      return false;
    }

    auto content_kv = content_handles_.find(link_id);

    if (content_kv == content_handles_.end()) {
      FX_LOGS(ERROR) << "SetLinkProperties failed, link_id " << link_id << " not found";
      return false;
    }

    auto link_kv = child_links_.find(content_kv->second);

    if (link_kv == child_links_.end()) {
      FX_LOGS(ERROR) << "SetLinkProperties failed, content_id " << link_id << " is not a Link";
      return false;
    }

    // Callers do not have to provide a new logical size on every call to SetLinkProperties, but if
    // they do, it must have positive X and Y values.
    if (properties.has_logical_size()) {
      auto logical_size = properties.logical_size();
      if (logical_size.x <= 0.f || logical_size.y <= 0.f) {
        FX_LOGS(ERROR) << "SetLinkProperties failed, logical_size components must be positive, "
                       << "given (" << logical_size.x << ", " << logical_size.y << ")";
        return false;
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

    return true;
  });
}

void Flatland::SetLinkSize(ContentId link_id, Vec2 size) {
  pending_operations_.push_back([=, size = std::move(size)]() {
    if (link_id == 0) {
      FX_LOGS(ERROR) << "SetLinkSize called with link_id zero.";
      return false;
    }

    if (size.x <= 0.f || size.y <= 0.f) {
      FX_LOGS(ERROR) << "SetLinkSize failed, size components must be positive, given (" << size.x
                     << ", " << size.y << ")";
      return false;
    }

    auto content_kv = content_handles_.find(link_id);

    if (content_kv == content_handles_.end()) {
      FX_LOGS(ERROR) << "SetLinkSize failed, link_id " << link_id << " not found";
      return false;
    }

    auto link_kv = child_links_.find(content_kv->second);

    if (link_kv == child_links_.end()) {
      FX_LOGS(ERROR) << "SetLinkSize failed, content_id " << link_id << " is not a Link";
      return false;
    }

    FX_DCHECK(link_kv->second.link.importer.valid());

    link_kv->second.size = std::move(size);
    UpdateLinkScale(link_kv->second);

    return true;
  });
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FX_LOGS(ERROR) << "ReleaseTransform called with transform_id zero";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FX_LOGS(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    bool erased_from_graph = transform_graph_.ReleaseTransform(transform_kv->second);
    FX_DCHECK(erased_from_graph);
    transforms_.erase(transform_kv);

    return true;
  });
}

void Flatland::ReleaseLink(ContentId link_id,
                           fuchsia::ui::scenic::internal::Flatland::ReleaseLinkCallback callback) {
  pending_operations_.push_back([this, link_id, callback = std::move(callback)]() {
    if (link_id == 0) {
      FX_LOGS(ERROR) << "ReleaseLink called with link_id zero";
      return false;
    }

    auto content_kv = content_handles_.find(link_id);

    if (content_kv == content_handles_.end()) {
      FX_LOGS(ERROR) << "ReleaseLink failed, link_id " << link_id << " not found";
      return false;
    }

    auto link_kv = child_links_.find(content_kv->second);

    if (link_kv == child_links_.end()) {
      FX_LOGS(ERROR) << "ReleaseLink failed, content_id " << link_id << " is not a Link";
      return false;
    }

    ContentLinkToken return_token;

    // If the link is still valid, return the original token. If not, create an orphaned
    // zx::eventpair and return it since the ObjectLinker does not retain the orphaned token.
    auto link_token = link_kv->second.link.importer.ReleaseToken();
    if (link_token.has_value()) {
      return_token.value = zx::eventpair(std::move(link_token.value()));
    } else {
      // |peer_token| immediately falls out of scope, orphaning |return_token|.
      zx::eventpair peer_token;
      zx::eventpair::create(0, &return_token.value, &peer_token);
    }

    bool child_removed = transform_graph_.RemoveChild(link_kv->second.link.graph_handle,
                                                      link_kv->second.link.link_handle);
    FX_DCHECK(child_removed);

    bool content_released = transform_graph_.ReleaseTransform(link_kv->second.link.graph_handle);
    FX_DCHECK(content_released);

    child_links_.erase(link_kv->second.link.graph_handle);
    content_handles_.erase(link_id);

    callback(std::move(return_token));

    return true;
  });
}

void Flatland::ReleaseImage(ContentId image_id) {
  pending_operations_.push_back([=]() {
    if (image_id == kInvalidId) {
      FX_LOGS(ERROR) << "ReleaseImage called with image_id zero";
      return false;
    }

    auto content_kv = content_handles_.find(image_id);

    if (content_kv == content_handles_.end()) {
      FX_LOGS(ERROR) << "ReleaseImage failed, image_id " << image_id << " not found";
      return false;
    }

    auto image_kv = image_metadatas_.find(content_kv->second);

    if (image_kv == image_metadatas_.end()) {
      FX_LOGS(ERROR) << "ReleaseImage failed, content_id " << image_id << " is not an Image";
      return false;
    }

    bool erased_from_graph = transform_graph_.ReleaseTransform(content_kv->second);
    FX_DCHECK(erased_from_graph);

    // Even though the handle is released, it may still be referenced by client Transforms. The
    // image_metadatas_ map preserves the entry until it shows up in the dead_transforms list.
    content_handles_.erase(image_id);

    return true;
  });
}

TransformHandle Flatland::GetRoot() const {
  return parent_link_ ? parent_link_->link_origin : local_root_;
}

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
