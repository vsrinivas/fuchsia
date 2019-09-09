// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_SCENIC_FLATLAND_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/fit/function.h>

#include <unordered_map>
#include <unordered_set>

namespace flatland {

// This is a WIP implementation of the 2D Layer API. It currently exists solely to run unit tests,
// and to provide a platform for features to be iterated and implemented over time.
class Flatland : public fuchsia::ui::scenic::internal::Flatland {
 public:
  using TransformId = uint64_t;
  using NodeHandle = uint64_t;

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
  static constexpr TransformId kRootId = 0;

  using EdgeMap = std::multimap<NodeHandle, NodeHandle>;
  using TransformMap = std::map<TransformId, NodeHandle>;
  using NodeSet = std::unordered_set<NodeHandle>;
  // This uint64_t is an index to an earlier element in the vector.
  using TopologicalVector = std::vector<std::pair<NodeHandle, uint64_t>>;

  class TopologicalData {
   public:
    TopologicalData() = default;
    TopologicalData(const TransformMap& root_nodes, const EdgeMap& edges);

    const NodeSet& cyclical_nodes() const { return cyclical_nodes_; }
    const NodeSet& live_nodes() const { return live_nodes_; }

   private:
    void Traverse(NodeHandle id, const EdgeMap& edges);

    TopologicalVector sorted_nodes_;
    NodeSet live_nodes_;
    NodeSet cyclical_nodes_;
  };

  std::vector<fit::function<bool()>> pending_operations_;
  uint32_t num_presents_remaining_ = 1;

  NodeHandle next_handle_ = 1;

  TransformMap transforms_;
  EdgeMap edges_;
  TopologicalData topological_data_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_SCENIC_FLATLAND_H_
