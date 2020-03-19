// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <lib/zx/eventpair.h>

#include <memory>

#include "src/lib/fxl/logging.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::Error;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LinkProperties;
using fuchsia::ui::scenic::internal::Orientation;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {

Flatland::Flatland(const std::shared_ptr<LinkSystem>& link_system,
                   const std::shared_ptr<UberStructSystem>& uber_struct_system)
    : link_system_(link_system),
      uber_struct_system_(uber_struct_system),
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
  FXL_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());

  // TODO(36166): Once the 2D scene graph is externalized, don't commit changes if a cycle is
  // detected. Instead, kill the channel and remove the sub-graph from the global graph.
  success &= data.cyclical_edges.empty();

  if (success) {
    FXL_DCHECK(data.sorted_transforms[0].handle == root_handle);

    auto uber_struct = std::make_unique<UberStruct>();
    uber_struct->local_topology = std::move(data.sorted_transforms);

    for (const auto& [handle, child_link] : child_links_) {
      LinkProperties initial_properties;
      fidl::Clone(child_link.properties, &initial_properties);
      uber_struct->link_properties[child_link.link.graph_handle] = std::move(initial_properties);
    }

    for (const auto& [handle, matrix_data] : matrices_) {
      uber_struct->local_matrices[handle] = matrix_data.GetMatrix();
    }

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
      FXL_LOG(ERROR) << "LinkToParent failed, GraphLinkToken was invalid";
      return false;
    });
    return;
  }

  FXL_DCHECK(link_system_);

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
      FXL_DCHECK(child_removed);

      bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
      FXL_DCHECK(transform_released);
    }
    bool child_added = transform_graph_.AddChild(link.link_origin, local_root_);
    FXL_DCHECK(child_added);
    parent_link_ = std::move(link);
    return true;
  });
}

void Flatland::UnlinkFromParent(
    fuchsia::ui::scenic::internal::Flatland::UnlinkFromParentCallback callback) {
  pending_operations_.push_back([this, callback = std::move(callback)]() {
    if (!parent_link_) {
      FXL_LOG(ERROR) << "UnlinkFromParent failed, no existing parent link";
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
    FXL_DCHECK(child_removed);

    bool transform_released = transform_graph_.ReleaseTransform(parent_link_->link_origin);
    FXL_DCHECK(transform_released);

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
      FXL_LOG(ERROR) << "CreateTransform called with transform_id 0";
      return false;
    }

    if (transforms_.count(transform_id)) {
      FXL_LOG(ERROR) << "CreateTransform called with pre-existing transform_id " << transform_id;
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
      FXL_LOG(ERROR) << "SetTranslation called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "SetTranslation failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetTranslation(translation);

    return true;
  });
}

void Flatland::SetOrientation(TransformId transform_id, Orientation orientation) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "SetOrientation called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "SetOrientation failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetOrientation(orientation);

    return true;
  });
}

void Flatland::SetScale(TransformId transform_id, Vec2 scale) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "SetScale called with transform_id 0";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "SetScale failed, transform_id " << transform_id << " not found";
      return false;
    }

    matrices_[transform_kv->second].SetScale(scale);

    return true;
  });
}

void Flatland::AddChild(TransformId parent_transform_id, TransformId child_transform_id) {
  pending_operations_.push_back([=]() {
    if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "AddChild called with transform_id zero";
      return false;
    }

    auto parent_global_kv = transforms_.find(parent_transform_id);
    auto child_global_kv = transforms_.find(child_transform_id);

    if (parent_global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "AddChild failed, parent_transform_id " << parent_transform_id
                     << " not found";
      return false;
    }

    if (child_global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "AddChild failed, child_transform_id " << child_transform_id
                     << " not found";
      return false;
    }

    bool added = transform_graph_.AddChild(parent_global_kv->second, child_global_kv->second);

    if (!added) {
      FXL_LOG(ERROR) << "AddChild failed, connection already exists between parent "
                     << parent_transform_id << " and child " << child_transform_id;
    }

    return added;
  });
}

void Flatland::RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) {
  pending_operations_.push_back([=]() {
    if (parent_transform_id == kInvalidId || child_transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "RemoveChild failed, transform_id " << parent_transform_id << " not found";
      return false;
    }

    auto parent_global_kv = transforms_.find(parent_transform_id);
    auto child_global_kv = transforms_.find(child_transform_id);

    if (parent_global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "RemoveChild failed, parent_transform_id " << parent_transform_id
                     << " not found";
      return false;
    }

    if (child_global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "RemoveChild failed, child_transform_id " << child_transform_id
                     << " not found";
      return false;
    }

    bool removed = transform_graph_.RemoveChild(parent_global_kv->second, child_global_kv->second);

    if (!removed) {
      FXL_LOG(ERROR) << "RemoveChild failed, connection between parent " << parent_transform_id
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
      FXL_LOG(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    bool added = transform_graph_.AddChild(local_root_, global_kv->second);
    FXL_DCHECK(added);
    return true;
  });
}

void Flatland::CreateLink(LinkId link_id, ContentLinkToken token, LinkProperties properties,
                          fidl::InterfaceRequest<ContentLink> content_link) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    pending_operations_.push_back([]() {
      FXL_LOG(ERROR) << "CreateLink failed, ContentLinkToken was invalid";
      return false;
    });
    return;
  }

  if (!properties.has_logical_size()) {
    pending_operations_.push_back([]() {
      FXL_LOG(ERROR) << "CreateLink must be provided with LinkProperties with a logical size.";
      return false;
    });
    return;
  }

  FXL_DCHECK(link_system_);

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
          return false;
        }

        if (child_links_.count(link_id)) {
          return false;
        }

        bool child_added = transform_graph_.AddChild(link.graph_handle, link.link_handle);
        FXL_DCHECK(child_added);

        child_links_[link_id] = {.link = std::move(link), .properties = std::move(properties)};

        return true;
      });
}

void Flatland::SetLinkOnTransform(LinkId link_id, TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "SetLinkOnTransform called with transform_id zero";
      return false;
    }

    auto transform_kv = transforms_.find(transform_id);

    if (transform_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    if (link_id == 0) {
      transform_graph_.ClearPriorityChild(transform_kv->second);
      return true;
    }

    auto link_kv = child_links_.find(link_id);

    if (link_kv == child_links_.end()) {
      FXL_LOG(ERROR) << "SetLinkOnTransform failed, link_id " << link_id << " not found";
      return false;
    }

    transform_graph_.SetPriorityChild(transform_kv->second, link_kv->second.link.graph_handle);
    return true;
  });
}

void Flatland::SetLinkProperties(LinkId id, LinkProperties properties) {
  pending_operations_.push_back([=, properties = std::move(properties)]() mutable {
    if (id == 0) {
      return false;
    }

    auto link_kv = child_links_.find(id);

    if (link_kv == child_links_.end()) {
      return false;
    }

    FXL_DCHECK(link_kv->second.link.importer.valid());

    link_kv->second.properties = std::move(properties);

    return true;
  });
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "ReleaseTransform called with transform_id zero";
      return false;
    }

    auto iter = transforms_.find(transform_id);

    if (iter == transforms_.end()) {
      FXL_LOG(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    bool erased_from_graph = transform_graph_.ReleaseTransform(iter->second);
    FXL_DCHECK(erased_from_graph);
    transforms_.erase(iter);

    return true;
  });
}

void Flatland::ReleaseLink(LinkId link_id,
                           fuchsia::ui::scenic::internal::Flatland::ReleaseLinkCallback callback) {
  pending_operations_.push_back([this, link_id, callback = std::move(callback)]() {
    if (link_id == 0) {
      FXL_LOG(ERROR) << "ReleaseLink called with link_id zero";
      return false;
    }

    auto link_kv = child_links_.find(link_id);

    if (link_kv == child_links_.end()) {
      FXL_LOG(ERROR) << "ReleaseLink failed, link_id " << link_id << " not found";
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
    FXL_DCHECK(child_removed);

    bool content_released = transform_graph_.ReleaseTransform(link_kv->second.link.graph_handle);
    FXL_DCHECK(content_released);

    child_links_.erase(link_id);

    callback(std::move(return_token));

    return true;
  });
}

TransformHandle Flatland::GetRoot() const {
  return parent_link_ ? parent_link_->link_origin : local_root_;
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
  matrix_ = glm::scale(glm::rotate(glm::translate(glm::mat3(), translation_), angle_), scale_);
}

glm::mat3 Flatland::MatrixData::GetMatrix() const { return matrix_; }

}  // namespace flatland
