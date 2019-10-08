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

Flatland::Flatland() : Flatland(/* linker */ nullptr) {}

Flatland::Flatland(const std::shared_ptr<ObjectLinker>& linker) : linker_(linker) {}

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

  topological_data_ = TopologicalData(transforms_, children_);

  // TODO(36166): Once the 2D scene graph is externalized, don't commit changes if a cycle is
  // detected. Instead, kill the channel and remove the sub-graph from the global graph.
  success &= topological_data_.cyclical_nodes().empty();

  // Clean up dead objects.
  for (auto iter = children_.begin(); iter != children_.end();) {
    if (!topological_data_.live_nodes().count(iter->first)) {
      iter = children_.erase(iter);
    } else {
      ++iter;
    }
  }

  if (success) {
    // TODO(36161): Once present operations can be pipelined, this variable will change state based
    // on the number of outstanding Present calls. Until then, this call is synchronous, and we can
    // always return 1 as the number of remaining presents.
    callback(fit::ok(num_presents_remaining_));
  } else {
    callback(fit::error(Error::BAD_OPERATION));
  }
}

void Flatland::LinkToParent(GraphLinkToken token, fidl::InterfaceRequest<GraphLink> graph_link) {
  FXL_DCHECK(linker_);

  // This portion of the method is not feed forward. This makes it possible for clients to receive
  // layout information before this operation has been presented. By initializing the link
  // immediately, parents can inform children of layout changes, and child clients can perform
  // layout decisions before their first call to Present().
  auto link = std::make_unique<ParentLink>();
  auto impl = std::make_shared<ContentLinkImpl>();
  link->impl = impl;
  link->exporter = linker_->CreateExport(std::move(token.value), /* error_reporter */ nullptr);

  auto graph_link_request = std::make_unique<GraphLinkRequest>();
  graph_link_request->fidl_request = std::move(graph_link);

  link->exporter.Initialize(
      graph_link_request.get(),
      /* link_resolved = */
      [this, impl = std::move(impl)](ContentLinkRequest* request) {
        // Set up the link here, so that the channel is initialized, but don't actually change the
        // link in our member variable until present is called.
        //
        // TODO(37597): Calling LinkToParent()) a second time should clean up the previous link in
        // the binding set.
        content_link_bindings_.AddBinding(impl, std::move(request->fidl_request));
      },
      /* link_failed = */
      [request = std::move(graph_link_request)] {
        // TODO(36173): This closure exists solely to keep the request allocation alive. Switch to
        // move semantics once they become available.
      });

  // This portion of the method is feed-forward. Our Link should not actually be changed until
  // Present() is called, so that the update to the Link is atomic with all other operations in the
  // batch.
  pending_operations_.push_back([this, link = std::move(link)]() mutable {
    parent_link_ = std::move(link);
    parent_link_->impl->UpdateLinkStatus(
        fuchsia::ui::scenic::internal::ContentLinkStatus::CONTENT_HAS_PRESENTED);
    return true;
  });
}

void Flatland::ClearGraph() {
  pending_operations_.push_back([=]() {
    transforms_.clear();
    children_.clear();
    topological_data_ = TopologicalData();
    return true;
  });
}

void Flatland::CreateTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    // We store the global id of the root transform in the transform map under ID 0, since ID 0 is
    // invalid for user-generated transforms. This allows a single submission to the topological
    // sorter, instead of sending both the transform map, and the single additional root index.
    static_assert(
        kRootId == kInvalidId,
        "The invalid ID is reserved for placing the root transform in the transform map.");

    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "CreateTransform called with transform_id 0";
      return false;
    }

    if (transforms_.count(transform_id)) {
      FXL_LOG(ERROR) << "CreateTransform called with pre-existing transform_id " << transform_id;
      return false;
    }

    GlobalHandle handle{next_global_id_++};

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

    const auto& parent_global_id = parent_global_kv->second;
    const auto& child_global_id = child_global_kv->second;

    auto [key_iter, end_iter] = children_.equal_range(parent_global_id);
    for (; key_iter != end_iter; ++key_iter) {
      if (key_iter->second == child_global_id) {
        FXL_LOG(ERROR) << "AddChild failed, link already exists between parent "
                       << parent_transform_id << " and child " << child_transform_id;
        return false;
      }
    }

    children_.insert(end_iter, {parent_global_kv->second, child_global_kv->second});
    return true;
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

    const auto& parent_global_id = parent_global_kv->second;
    const auto& child_global_id = child_global_kv->second;

    auto [iter, end_iter] = children_.equal_range(parent_global_id);
    for (; iter != end_iter; ++iter) {
      if (iter->second == child_global_id) {
        children_.erase(iter);
        return true;
      }
    }

    FXL_LOG(ERROR) << "RemoveChild failed, link between parent " << parent_transform_id
                   << " and child " << child_transform_id << " not found";
    return false;
  });
}

void Flatland::SetRootTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    // The root transform is stored in the TransformMap, where all the mappings from user-generated
    // IDs to global IDs reside. Since the user is not allowed to use zero as a user-generated ID,
    // we store the root's global ID under that special key.
    //
    //  This way, the root is kept alive even if the client releases that particular non-zero user
    //  id. This also makes it possible to submit all live nodes to the topological sorter in a
    //  coherent way.

    // SetRootTransform(0) is special -- it clears the existing root transform.
    if (transform_id == kInvalidId) {
      transforms_.erase(kRootId);
      return true;
    }

    auto global_kv = transforms_.find(transform_id);
    if (global_kv == transforms_.end()) {
      FXL_LOG(ERROR) << "SetRootTransform failed, transform_id " << transform_id << " not found";
      return false;
    }

    transforms_.insert({kRootId, global_kv->second});
    return true;
  });
}

void Flatland::CreateLink(LinkId link_id, ContentLinkToken token, LinkProperties properties,
                          fidl::InterfaceRequest<ContentLink> content_link) {
  // We can initialize the link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method.
  auto impl = std::make_shared<GraphLinkImpl>();
  auto link = std::make_unique<ChildLink>();
  link->impl = impl;
  link->importer = linker_->CreateImport(std::move(token.value), /* error_reporter */ nullptr);

  auto content_link_request = std::make_unique<ContentLinkRequest>();
  content_link_request->fidl_request = std::move(content_link);
  link->importer.Initialize(
      content_link_request.get(),
      /* link_resolved = */
      [this, impl = std::move(impl)](GraphLinkRequest* request) {
        graph_link_bindings_.AddBinding(impl, std::move(request->fidl_request));
      },
      /* link_failed = */
      [request = std::move(content_link_request)]() {
        // TODO(36173): This closure exists solely to keep the request allocation alive. Switch to
        // move semantics once they become available.
      });

  // This is the feed-forward portion of the method. Here, we add the link to the map, and
  // initialize its layout with the desired properties.
  pending_operations_.push_back(
      [=, link = std::move(link), properties = std::move(properties)]() mutable {
        if (link_id == 0)
          return false;

        if (child_links_.count(link_id))
          return false;

        LayoutInfo info;
        if (properties.has_logical_size())
          info.set_logical_size(properties.logical_size());
        else
          info.set_logical_size(Vec2{kDefaultLayoutSize, kDefaultLayoutSize});

        link->impl->UpdateLayoutInfo(std::move(info));
        child_links_[link_id] = std::move(link);
        return true;
      });
}

void Flatland::SetLinkProperties(LinkId id, LinkProperties properties) {
  // This entire method is feed-forward, but we need a custom closure to capture the properties via
  // move semantics.
  pending_operations_.push_back([=, properties = std::move(properties)]() mutable {
    if (id == 0)
      return false;

    auto link_kv = child_links_.find(id);

    if (link_kv == child_links_.end())
      return false;

    FXL_DCHECK(link_kv->second->impl);
    FXL_DCHECK(link_kv->second->importer.valid());
    LayoutInfo info;
    if (properties.has_logical_size())
      info.set_logical_size(properties.logical_size());
    else
      info.set_logical_size(Vec2{kDefaultLayoutSize, kDefaultLayoutSize});

    link_kv->second->impl->UpdateLayoutInfo(std::move(info));
    return true;
  });
}

void Flatland::ReleaseTransform(TransformId transform_id) {
  pending_operations_.push_back([=]() {
    if (transform_id == kInvalidId) {
      FXL_LOG(ERROR) << "ReleaseTransform called with transform_id zero";
      return false;
    }

    bool erased = transforms_.erase(transform_id) != 0;

    if (!erased)
      FXL_LOG(ERROR) << "ReleaseTransform failed, transform_id " << transform_id << " not found";

    return erased;
  });
}

Flatland::TopologicalData::TopologicalData(const TransformMap& initial_nodes,
                                           const ChildMap& edges) {
  for (auto [id, global_id] : initial_nodes) {
    // Skip the node if we've already visited it.
    if (live_nodes_.count(global_id)) {
      continue;
    }

    // Insert this node, and all children, into the sorted data.
    Traverse(global_id, edges);
  }

  // If we have a root transform, it better be the first element in the topological sort.
  if (initial_nodes.count(kRootId)) {
    FXL_DCHECK(sorted_nodes_[0].first == initial_nodes.at(kRootId));
  }
}

void Flatland::TopologicalData::Traverse(GlobalHandle start, const ChildMap& edges) {
  std::vector<std::pair<ChildMap::const_iterator, ChildMap::const_iterator>> iterator_stack;
  std::vector<GlobalHandle> ancestors;

  uint64_t current_parent_index = sorted_nodes_.size();
  sorted_nodes_.push_back({start, current_parent_index});
  live_nodes_.insert(start);
  iterator_stack.push_back(edges.equal_range(start));
  ancestors.push_back(start);

  while (!iterator_stack.empty()) {
    auto& [child_iter, end_iter] = iterator_stack.back();

    if (child_iter == end_iter) {
      iterator_stack.pop_back();
      ancestors.pop_back();
      FXL_DCHECK(current_parent_index < sorted_nodes_.size());
      current_parent_index = sorted_nodes_[current_parent_index].second;
      continue;
    }

    const GlobalHandle child = child_iter->second;
    ++child_iter;

    // Search from the bottom of the stack (since it's more likely), looking for a cycle.
    if (std::find(ancestors.crbegin(), ancestors.crend(), child) != ancestors.crend()) {
      cyclical_nodes_.insert(child);
    } else {
      int new_parent_index = sorted_nodes_.size();
      sorted_nodes_.push_back({child, current_parent_index});
      live_nodes_.insert(child);
      iterator_stack.push_back(edges.equal_range(child));
      ancestors.push_back(child);
      current_parent_index = new_parent_index;
    }
  }
}

}  // namespace flatland
