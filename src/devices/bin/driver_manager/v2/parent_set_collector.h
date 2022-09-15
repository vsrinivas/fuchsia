// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_PARENT_SET_COLLECTOR_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_PARENT_SET_COLLECTOR_H_

#include <fidl/fuchsia.driver.index/cpp/wire.h>

#include "src/devices/bin/driver_manager/v2/node.h"

namespace dfv2 {

// |ParentSetCollector| wraps functionality for collecting multiple parent nodes for composites.
// The parent set starts out empty and gets nodes added to it until it is complete. Once complete
// it will return a vector containing all the parent node pointers.
class ParentSetCollector {
 public:
  explicit ParentSetCollector(size_t size) : size_(size), parents_(size) {}

  // Add a node to the parent set at the specified index.
  // Caller should check that |ContainsNode| is false for the index before calling this.
  // Only a weak_ptr of the node is stored by this class (until collection in GetIfComplete).
  void AddNode(uint32_t index, std::weak_ptr<Node> node);

  // Remove a node at a specific index from the parent set.
  void RemoveNode(uint32_t index);

  // Returns the completed parent set if it is a completed set.
  // Otherwise a nullopt.
  // The lifetime of the Node objects is managed by their parent nodes. This method
  // will only return a vector where none of the elements are null pointers.
  std::optional<std::vector<Node*>> GetIfComplete();

  // Returns whether the parent set is occupied at the index.
  bool ContainsNode(uint32_t index) const;

  size_t size() const { return size_; }
  const std::weak_ptr<Node>& get(uint32_t index) const { return parents_[index]; }

 private:
  size_t size_;

  // Nodes are stored as weak_ptrs. Only when trying to collect the completed set are they
  // locked into shared_ptrs and validated to not be null.
  std::vector<std::weak_ptr<Node>> parents_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_PARENT_SET_COLLECTOR_H_
