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

Flatland::Flatland(const std::shared_ptr<ObjectLinker>& linker,
                   const std::shared_ptr<TopologySystem>& system)
    : linker_(linker),
      topology_system_(system),
      transform_graph_(system->CreateGraph()),
      link_origin_(transform_graph_.CreateTransform()) {}

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
  ParentLink link({
      .impl = std::make_shared<ContentLinkImpl>(),
      .exporter = linker_->CreateExport(std::move(graph_link), std::move(token.value),
                                        /* error_reporter */ nullptr),
  });

  link.exporter.Initialize(
      /* link_resolved = */
      [this, impl = link.impl](fidl::InterfaceRequest<ContentLink> request) {
        // Set up the link here, so that the channel is initialized, but don't actually change the
        // link in our member variable until present is called.
        content_link_bindings_.AddBinding(impl, std::move(request));
      },
      /* link_invalidated = */
      [this, impl = link.impl](bool on_link_destruction) {
        if (!on_link_destruction) {
          content_link_bindings_.RemoveBinding(impl);
        }
      });

  // This portion of the method is feed-forward. Our Link should not actually be changed until
  // Present() is called, so that the update to the Link is atomic with all other operations in the
  // batch.
  pending_operations_.push_back([this, link = std::move(link)]() mutable {
    parent_link_ = std::move(link);
    parent_link_.impl->UpdateLinkStatus(
        fuchsia::ui::scenic::internal::ContentLinkStatus::CONTENT_HAS_PRESENTED);
    return true;
  });
}

void Flatland::ClearGraph() {
  pending_operations_.push_back([=]() {
    transforms_.clear();
    // We always preserve the link origin when clearing the graph.
    transform_graph_.ResetGraph(link_origin_);
    child_links_.clear();
    graph_link_bindings_.CloseAll();
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
  ChildLink link({
      .impl = std::make_shared<GraphLinkImpl>(),
      .importer = linker_->CreateImport(std::move(content_link), std::move(token.value),
                                        /* error_reporter */ nullptr),
  });

  link.importer.Initialize(
      /* link_resolved = */
      [this, impl = link.impl](fidl::InterfaceRequest<GraphLink> request) {
        graph_link_bindings_.AddBinding(impl, std::move(request));
      },
      /* link_invalidated = */
      [this, impl = link.impl](bool on_link_destruction) {
        if (!on_link_destruction) {
          graph_link_bindings_.RemoveBinding(impl);
        }
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

        link.impl->UpdateLayoutInfo(std::move(info));
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

    FXL_DCHECK(link_kv->second.impl);
    FXL_DCHECK(link_kv->second.importer.valid());
    LayoutInfo info;
    if (properties.has_logical_size())
      info.set_logical_size(properties.logical_size());
    else
      info.set_logical_size(Vec2{kDefaultLayoutSize, kDefaultLayoutSize});

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

}  // namespace flatland
