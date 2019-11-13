// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_GRAPH_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_GRAPH_H_

#include <map>
#include <unordered_set>
#include <vector>

#include "src/ui/scenic/lib/flatland/transform_handle.h"

namespace flatland {

// Represents a set of transforms within the scope of a single Flatland instance.
//
// This class is intended to be used as a helper class for the Flatland protocol. As an internal
// implementation detail, it does not perform many error checks on its own. For example,
// cycles are allowed between any two transforms, and child transforms do not have to be constructed
// from this graph. Some invariants are enforced only by DCHECK. Do not give clients direct access
// to this class without first filtering inputs and outputs.
class TransformGraph {
 public:
  // An entry in a TopologyVector. See TopologyVector for more information.
  struct TopologyEntry {
    TransformHandle handle;
    uint64_t parent_index;

    bool operator==(const TopologyEntry& rhs) const {
      return handle == rhs.handle && parent_index == rhs.parent_index;
    }
  };

  // A list of transforms, sorted in topological (i.e., depth-first) order. For each transform,
  // there is also a parent index -- the index in this vector of that element's parent. Since
  // transforms can appear in the list multiple times, each element in the list stores its own
  // parent index. The parent index should always be an earlier element in the list, since the
  // entries are sorted topologically.
  using TopologyVector = std::vector<TopologyEntry>;

  // A collection of directed edges, the key in the map is the parent transform and the values are
  // the children.
  using ChildMap = std::multimap<TransformHandle, TransformHandle>;

  // A collection of transforms.
  using TransformSet = std::unordered_set<TransformHandle>;

  // For testing, equivalent to calling TransformGraph(0).
  TransformGraph();
  TransformGraph(uint64_t graph_id);
  ~TransformGraph() = default;

  // Creates a new transform for use with this graph, and adds it to the graph's internal working
  // set of transforms. Released descendants of this transform will be kept alive until this
  // transform is released, or until the edges to the descendants are removed.
  TransformHandle CreateTransform();

  // Releases a transform. If no other transforms reference this transform through an edge, this
  // transform will appear in the dead_transforms list the next time ComputeAndCleanup() is called.
  // Returns false if the transform was not created by CreateTransform(), or if the transform has
  // already been released.
  bool ReleaseTransform(TransformHandle handle);

  // Creates a directed edge from the parent to the child transform. Children are kept alive by
  // unreleased parents. The parent transform should be an unreleased transform created by calling
  // CreateTransform() on this object, but this is only enforced by DCHECK. Returns false if the
  // child transform is already a child of the parent transform.
  bool AddChild(TransformHandle parent, TransformHandle child);

  // Removes a directed edge from the parent and child transform. This may result in one or more
  // transforms being added to the dead_transforms list the next time ComputeAndCleanup() is called.
  // The parent transform should be an unreleased transform created by calling CreateTransform() on
  // this object, but this is only enforced by DCHECK. Returns false if the child transform was not
  // previously a child of the parent transform.
  bool RemoveChild(TransformHandle parent, TransformHandle child);

  // Removes all child edges from the parent transform.
  void ClearChildren(TransformHandle parent);

  // Clears all data from this entire graph, with one exception. The passed in handle is maintained
  // as a member of the working set, but with all existing children removed.
  //
  // This operation does not result in handles being reported as dead via ComputeAndCleanup().
  void ResetGraph(TransformHandle exception);

  // The return value type for ComputeAndCleanup().
  struct TopologyData {
    // A topologically sorted list of transforms. This will be the set of all transforms visitable
    // from the "start" transform, through the directed edges of this graph.
    TopologyVector sorted_transforms;

    // A set of edges that, when removed from the graph, breaks all existing cycles.
    ChildMap cyclical_edges;

    // The set of nodes that went out of scope since the last call to ComputeAndCleanup().
    TransformSet dead_transforms;

    // The number of iterations it took to compute this data. If this value is equal or larger than
    // the argument passed into ComputeAndCleanup(), the rest of the data in this struct may not be
    // accurate.
    uint64_t iterations = 0;
  };

  // Generates a topology vector rooted at the "start" transform, as well as additional data
  // involving dead transforms, cycles, and iterations. See the TopologyData struct for more
  // information.
  //
  // If max_iterations is reached, the transform graph will be in an invalid state, and should be
  // reset before any further methods are called.
  TopologyData ComputeAndCleanup(TransformHandle start, uint64_t max_iterations);

 private:
  // A static helper function, returns the TopologyVector rooted at the "start" transform,
  // following edges defined in the "children" map. Cycles are returned through the out parameter
  // "cycles".
  //
  // Computation is halted once the return vector has grown to max_length in size.
  static TopologyVector Traverse(TransformHandle start, const ChildMap& children, ChildMap* cycles,
                                 uint64_t max_length);

  const uint64_t graph_id_ = 0;

  // The id for the next transform generated by CreateTransform().
  uint64_t next_transform_id_ = 0;

  // The set of unreleased transforms.
  TransformSet working_set_;

  // The set of all alive transforms.
  TransformSet live_set_;

  // A multimap. Each key is a global handle. The set of values are the children for that handle.
  ChildMap children_;

  // This variable is only used for DCHECKs. If ComputeAndCleanup() reaches its iteration limit,
  // this class will be in an invalid state, and most functions should not be called until the graph
  // is reset.
  bool is_valid_ = true;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_GRAPH_H_
