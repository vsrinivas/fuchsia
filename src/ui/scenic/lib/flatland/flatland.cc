// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include "src/lib/fxl/logging.h"

using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::Error;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;
using fuchsia::ui::scenic::internal::Vec2;

namespace flatland {

// Links created without a logical_size field in the properties table should be initialized to a
// unit square.
static constexpr float kDefaultLayoutSize = 1.0f;

Flatland::Flatland(const std::shared_ptr<LinkSystem>& link_system,
                   const std::shared_ptr<TopologySystem>& topology_system)
    : link_system_(link_system),
      topology_system_(topology_system),
      transform_graph_(topology_system_->CreateGraph()),
      link_origin_(transform_graph_.CreateTransform()) {}

Flatland::~Flatland() {
  if (parent_link_) {
    topology_system_->ClearLocalTopology(parent_link_->link_handle);
  }
  topology_system_->ClearLocalTopology(link_origin_);
}

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

  // TODO(40818): Decide on a proper limit on compute time for topological sorting.
  auto data =
      transform_graph_.ComputeAndCleanup(link_origin_, std::numeric_limits<uint64_t>::max());
  FXL_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());

  // TODO(36166): Once the 2D scene graph is externalized, don't commit changes if a cycle is
  // detected. Instead, kill the channel and remove the sub-graph from the global graph.
  success &= data.cyclical_edges.empty();

  if (success) {
    FXL_DCHECK(data.sorted_transforms[0].handle == link_origin_);
    topology_system_->SetLocalTopology(data.sorted_transforms);
    // TODO(36161): Once present operations can be pipelined, this variable will change state based
    // on the number of outstanding Present calls. Until then, this call is synchronous, and we can
    // always return 1 as the number of remaining presents.
    callback(fit::ok(num_presents_remaining_));
  } else {
    callback(fit::error(Error::BAD_OPERATION));
  }
}

void Flatland::LinkToParent(GraphLinkToken token, fidl::InterfaceRequest<GraphLink> graph_link) {
  FXL_DCHECK(link_system_);

  // This portion of the method is not feed forward. This makes it possible for clients to receive
  // layout information before this operation has been presented. By initializing the link
  // immediately, parents can inform children of layout changes, and child clients can perform
  // layout decisions before their first call to Present().
  LinkSystem::ParentLink link =
      link_system_->CreateParentLink(std::move(token), std::move(graph_link));

  // This portion of the method is feed-forward. Our Link should not actually be changed until
  // Present() is called, so that the update to the Link is atomic with all other operations in the
  // batch. The local topology from the |link_handle| to our |link_origin_| establishes the
  // transform hierarchy between the two instances.
  pending_operations_.push_back([this, link = std::move(link)]() mutable {
    if (parent_link_) {
      topology_system_->ClearLocalTopology(parent_link_->link_handle);
    }
    parent_link_ = std::move(link);
    // TODO(42750): thread safety guarantees for link impls
    parent_link_->impl->UpdateLinkStatus(
        fuchsia::ui::scenic::internal::ContentLinkStatus::CONTENT_HAS_PRESENTED);
    // TODO(42583): create link-specific topologies atomically.
    topology_system_->SetLocalTopology({{parent_link_->link_handle, 0}, {link_origin_, 0}});
    return true;
  });
}

void Flatland::ClearGraph() {
  pending_operations_.push_back([=]() {
    transforms_.clear();
    // We always preserve the link origin when clearing the graph.
    transform_graph_.ResetGraph(link_origin_);
    child_links_.clear();
    parent_link_ = LinkSystem::ParentLink();
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
    transform_graph_.ClearChildren(link_origin_);

    // SetRootTransform(0) is special -- it only clears the existing root transform.
    if (transform_id == kInvalidId) {
      return true;
    }

    auto global_kv = transforms_.find(transform_id);
    if (global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    bool added = transform_graph_.AddChild(link_origin_, global_kv->second);
    FXL_DCHECK(added);
    return true;
  });
}

void Flatland::CreateLink(LinkId link_id, ContentLinkToken token, LinkProperties properties,
                          fidl::InterfaceRequest<ContentLink> content_link) {
  // We can initialize the link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method.
  LinkSystem::ChildLink link =
      link_system_->CreateChildLink(std::move(token), std::move(content_link));

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

        LayoutInfo info;
        if (properties.has_logical_size()) {
          info.set_logical_size(properties.logical_size());
        } else {
          info.set_logical_size(Vec2{kDefaultLayoutSize, kDefaultLayoutSize});
        }

        // TODO(42750): thread safety guarantees for link impls
        link.impl->UpdateLayoutInfo(std::move(info));
        child_links_[link_id] = std::move(link);
        return true;
      });
}

void Flatland::SetLinkOnTransform(TransformId transform_id, LinkId link_id) {
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

    transform_graph_.SetPriorityChild(transform_kv->second, link_kv->second.link_handle);
    return true;
  });
}

void Flatland::SetLinkProperties(LinkId id, LinkProperties properties) {
  // This entire method is feed-forward, but we need a custom closure to capture the properties via
  // move semantics.
  pending_operations_.push_back([=, properties = std::move(properties)]() mutable {
    if (id == 0) {
      return false;
    }

    auto link_kv = child_links_.find(id);

    if (link_kv == child_links_.end()) {
      return false;
    }

    FXL_DCHECK(link_kv->second.impl);
    FXL_DCHECK(link_kv->second.importer.valid());
    LayoutInfo info;
    if (properties.has_logical_size()) {
      info.set_logical_size(properties.logical_size());
    } else {
      info.set_logical_size(Vec2{kDefaultLayoutSize, kDefaultLayoutSize});
    }

    // TODO(42750): thread safety guarantees for link impls
    link_kv->second.impl->UpdateLayoutInfo(std::move(info));
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

TransformHandle Flatland::GetLinkOrigin() const { return link_origin_; }

}  // namespace flatland
