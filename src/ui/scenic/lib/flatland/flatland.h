// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ui/scenic/lib/flatland/hanging_get_helper.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"

namespace flatland {

// A globally scoped handle. The current constructor allows the calling code to specify the
// internal ID. This class exists to get type-safety when comparing those integers to other IDs.
class GlobalHandle {
 public:
  GlobalHandle() = default;
  explicit GlobalHandle(uint64_t id) : id_(id) {}

  bool operator==(const GlobalHandle& rhs) const { return id_ == rhs.id_; }
  bool operator<(const GlobalHandle& rhs) const { return id_ < rhs.id_; }

 private:
  friend class std::hash<flatland::GlobalHandle>;
  uint64_t id_ = 0;
};

}  // namespace flatland

namespace std {

// A hash specialization for GlobalHandles, so that they can be stored in maps and multimaps.
template <>
struct hash<flatland::GlobalHandle> {
  size_t operator()(const flatland::GlobalHandle& h) const noexcept {
    return hash<uint64_t>{}(h.id_);
  }
};

}  // namespace std

namespace flatland {

// An implementation of the GraphLink protocol, consisting of hanging gets for various updateable
// pieces of information.
class GraphLinkImpl : public fuchsia::ui::scenic::internal::GraphLink {
 public:
  void UpdateLayoutInfo(fuchsia::ui::scenic::internal::LayoutInfo info) {
    layout_helper_.Update(std::move(info));
  }

  // |fuchsia::ui::scenic::internal::GraphLink|
  void GetLayout(GetLayoutCallback callback) override {
    // TODO(37750): Handle duplicate calls to a hanging get with an error, as the client is not
    // assuming the appropriate flow control.
    layout_helper_.SetCallback(
        [callback = std::move(callback)](fuchsia::ui::scenic::internal::LayoutInfo info) {
          callback(std::move(info));
        });
  }

 private:
  HangingGetHelper<fuchsia::ui::scenic::internal::LayoutInfo> layout_helper_;
};

// An implementation of the ContentLink protocol, consisting of hanging gets for various updateable
// pieces of information.
class ContentLinkImpl : public fuchsia::ui::scenic::internal::ContentLink {
 public:
  void UpdateLinkStatus(fuchsia::ui::scenic::internal::ContentLinkStatus status) {
    status_helper_.Update(std::move(status));
  }

  // |fuchsia::ui::scenic::internal::ContentLink|
  void GetStatus(GetStatusCallback callback) override {
    // TODO(37750): Handle duplicate calls to a hanging get with an error, as the client is not
    // assuming the appropriate flow control.
    status_helper_.SetCallback(std::move(callback));
  }

 private:
  HangingGetHelper<fuchsia::ui::scenic::internal::ContentLinkStatus> status_helper_;
};

// This is a WIP implementation of the 2D Layer API. It currently exists to run unit tests, and to
// provide a platform for features to be iterated and implemented over time.
class Flatland : public fuchsia::ui::scenic::internal::Flatland {
 public:
  using TransformId = uint64_t;
  using LinkId = uint64_t;

  // Linked Flatland instances only implement a small piece of link functionality. For now, directly
  // sharing link requests is a clean way to implement that functionality. This will become more
  // complicated as the Flatland API evolves.
  using ObjectLinker = scenic_impl::gfx::ObjectLinker<
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink>,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink>>;

  // Pass the same ObjectLinker to multiple Flatland instances to allow them to link to each other
  // through token transactions (e.g., LinkToParent(), CreateLink()).
  explicit Flatland(const std::shared_ptr<ObjectLinker>& linker);

  // For unit tests, this Flatland instance will not be able to link to other instances, either
  // through LinkToParent() or CreateLink().
  Flatland();

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  Flatland(const Flatland&) = delete;
  Flatland& operator=(const Flatland&) = delete;
  Flatland(Flatland&&) = delete;
  Flatland& operator=(Flatland&&) = delete;

  // |fuchsia::ui::scenic::internal::Flatland|
  void Present(PresentCallback callback) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void LinkToParent(
      fuchsia::ui::scenic::internal::GraphLinkToken token,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> graph_link) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ClearGraph() override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void CreateTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void AddChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetRootTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void CreateLink(
      LinkId link_id, fuchsia::ui::scenic::internal::ContentLinkToken token,
      fuchsia::ui::scenic::internal::LinkProperties properties,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink> content_link) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetLinkProperties(LinkId id,
                         fuchsia::ui::scenic::internal::LinkProperties properties) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseTransform(TransformId transform_id) override;

 private:
  // Users are not allowed to use zero as a transform ID.
  static constexpr TransformId kInvalidId = 0;
  // Because the user is never allowed to use the invalid ID as a key, we can use it as the key for
  // the root in the Transform map.
  static constexpr TransformId kRootId = kInvalidId;
  // This is the maximum number of pending Present() calls the user can have in flight. Since the
  // current implementation is synchronous, there can only be one call to Present() at a time.
  //
  // TODO(36161): Tune this number once we have a non-synchronous present flow.
  static constexpr uint32_t kMaxPresents = 1;

  using TransformMap = std::map<TransformId, GlobalHandle>;
  using ChildMap = std::multimap<GlobalHandle, GlobalHandle>;

  class TopologicalData {
   public:
    using NodeSet = std::unordered_set<GlobalHandle>;

    TopologicalData() = default;
    // Generates a topologically sorted list of nodes in a graph, starting from an initial set of
    // nodes, and a collection of directed edges (from parent to child).
    //
    // |initial_nodes| is passed as a TransformMap because it is easier for the client to pass the
    // whole map in, than to extract the global handles into a separate container. However, this
    // class doesn't make use of the TransformId mapping.
    TopologicalData(const TransformMap& initial_nodes, const ChildMap& edges);

    // A set of nodes that, when removed from the graph, breaks all existing cycles.
    const NodeSet& cyclical_nodes() const { return cyclical_nodes_; }

    // The set of all nodes that can be reached from the initial nodes, by traversing edges.
    const NodeSet& live_nodes() const { return live_nodes_; }

   private:
    using TopologicalVector = std::vector<std::pair<GlobalHandle, /* parent index */ uint64_t>>;

    void Traverse(GlobalHandle id, const ChildMap& edges);

    // The topological vector consists of the topological sorted list of the GlobalHandles of all
    // live nodes, along with the index of their parent in the sorted list. Because this is a
    // topological sort, the parent index will always be an earlier element in the list. Because
    // nodes can be visited through multiple paths, they may exist multiple times in the same sorted
    // list, each time with a different parent index.
    TopologicalVector sorted_nodes_;

    // The set of all nodes that can be reached from the initial nodes, by traversing edges.
    NodeSet live_nodes_;

    // A set of nodes that, when removed from the graph, breaks all existing cycles.
    NodeSet cyclical_nodes_;
  };

  // This is a strong reference to the GraphLink in the child Flatland instance. This makes it easy
  // for methods called on the ContentLink (e.g., SetLinkProperties()) to be transformed into output
  // events on the child's channel.
  struct ChildLink {
    std::shared_ptr<GraphLinkImpl> impl;
    ObjectLinker::ImportLink importer;
  };

  // This is a strong reference to the ContentLinkImpl in the parent Flatland instance. This makes
  // it easy for methods that effect the ContentLink (e.g., Present()) to be transformed into output
  // events on the parent's channel.
  struct ParentLink {
    std::shared_ptr<ContentLinkImpl> impl;
    ObjectLinker::ExportLink exporter;
  };

  // TODO(36451): ChildLinks need to be a heap allocations, because Link objects are unsafe to move
  // once they have been initialized. Remove the unique pointer indirection once BUG 36451 is fixed.
  using LinkMap = std::unordered_map<LinkId, std::unique_ptr<ChildLink>>;

  // An object linker shared between Flatland instances, so that links can be made between them.
  std::shared_ptr<ObjectLinker> linker_;

  // The set of operations that are pending a call to Present().
  std::vector<fit::function<bool()>> pending_operations_;

  // The number of pipelined Present() operations available to the client.
  uint32_t num_presents_remaining_ = kMaxPresents;

  // The next "global" id. This ID is not actually global as implemented, but handles are not
  // currently passed between instances. Once nodes are members of a global graph, this variable
  // will move out of the Flatland class.
  //
  // TODO(36174): Externalize these global IDs (along with all topological data) into a globally
  // scoped system.
  uint64_t next_global_id_ = kRootId + 1;

  // A map from user-generated id to global handle. This map constitutes the set of transforms that
  // can be referenced by the user through method calls. Keep in mind that additional transforms may
  // be kept alive through child references.
  TransformMap transforms_;

  // A multimap. Each key is a global handle. The set of values are the children for that handle.
  ChildMap children_;

  // The topological sorted list of handles, as computed from the TransformMap and ChildMap.
  //
  // TODO(36174): Externalize all topological data into a globally scoped system.
  TopologicalData topological_data_;

  // A mapping from user-generated id to ChildLink.
  LinkMap child_links_;

  // The link from this Flatland instance to our parent.
  //
  // TODO(36451): ParentLinks need to be a heap allocations, because Link objects are unsafe to move
  // once they have been initialized. Change this to a member variable once BUG 36451 is fixed.
  std::unique_ptr<ParentLink> parent_link_;

  // Any FIDL requests that have to be bound, are bound in these BindingSets. Despite using shared
  // pointers, the Impl classes are only owned by this Flatland instance, so the values in the
  // Impl classes can be updated immediately as operations are processed. The impl classes are
  // managed through a shared pointer because they are placed in two collections (e.g.
  // graph_link_bindings_ and child_links_) in response to two different events.
  fidl::BindingSet<fuchsia::ui::scenic::internal::GraphLink, std::shared_ptr<GraphLinkImpl>>
      graph_link_bindings_;
  fidl::BindingSet<fuchsia::ui::scenic::internal::ContentLink, std::shared_ptr<ContentLinkImpl>>
      content_link_bindings_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
