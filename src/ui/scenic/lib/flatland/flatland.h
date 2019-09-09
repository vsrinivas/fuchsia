// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/fit/function.h>

#include <unordered_map>
#include <unordered_set>

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

// This is a WIP implementation of the 2D Layer API. It currently exists solely to run unit tests,
// and to provide a platform for features to be iterated and implemented over time.
class Flatland : public fuchsia::ui::scenic::internal::Flatland {
 public:
  using TransformId = uint64_t;

  Flatland() = default;

  // |fuchsia::ui::scenic::internal::Flatland|
  void Present(PresentCallback callback) override;
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

    // A set of nodes that, when removed from the graph, break all existing cycles.
    const NodeSet& cyclical_nodes() const { return cyclical_nodes_; }

    // Returns the set of all nodes that can be reached from the initial nodes, by traversing edges.
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

    // The set of all nodes that can be visited, starting from the initial set of nodes, and
    // following edges to their children.
    NodeSet live_nodes_;

    // A set of nodes that, when removed from the graph, break all existing cycles.
    NodeSet cyclical_nodes_;
  };

  std::vector<fit::function<bool()>> pending_operations_;
  uint32_t num_presents_remaining_ = kMaxPresents;

  uint64_t next_global_id_ = kRootId + 1;

  // A map from user-generated id to global handle. This map constitutes the set of transforms that
  // can be referenced by the user through method calls. Keep in mind that additional transforms may
  // be kept alive through child references.
  TransformMap transforms_;

  // A multimap. Each key is a global handle. The set of values are the children for that handle.
  ChildMap children_;

  // The topological sorted list of handles, as computed from the TransformMap and ChildMap.
  TopologicalData topological_data_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
