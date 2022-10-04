// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_MANAGER_H_

#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <lib/inspect/cpp/inspect.h>

#include <unordered_map>

#include "src/devices/bin/driver_manager/v2/node.h"
#include "src/devices/bin/driver_manager/v2/parent_set_collector.h"

namespace dfv2 {

// |CompositeNodeManager| is used to manage the handling of matched composite drivers from
// the driver index. It will collect parent sets for drivers based on the driver url, once
// a parent set is complete it will remove it from its incomplete parents sets, create a child
// node under all the collected parent nodes, and returns the newly created node.
class CompositeNodeManager {
  using DriverUrl = std::string;
  using ParentSetIterator = std::unordered_multimap<DriverUrl, ParentSetCollector>::iterator;

 public:
  CompositeNodeManager(async_dispatcher_t* dispatcher, NodeManager* node_manager_);

  // If the `matched_driver` passed in completes a parent set, it creates a composite
  // node owned by all the parents and returns it.
  //
  // If the match does not create complete composite, the node will be kept track of internally
  // by the CompositeNodeManager and a ZX_ERR_NEXT error status is returned.
  //
  // If this returns a ZX_ERR_INVALID_ARGS error status, that means either the `matched_driver`
  // that was passed in was not valid, or the number of nodes in the parent sets previously
  // created for this driver url, did not match the number of nodes in the `matched_driver`.
  // In either case the node is not kept tracked of by the CompositeNodeManager and should be
  // orphaned by the client.
  zx::status<Node*> HandleMatchedCompositeInfo(
      Node& node, const fuchsia_driver_index::wire::MatchedCompositeInfo& matched_driver);

  void Inspect(inspect::Node& root) const;

 private:
  // Get an existing composite parent set that can add the `composite_info`, or creates
  // a new composite parent set if all existing ones are occupied already.
  // Returns an iterator to the existing or newly created one in the internal map.
  zx::status<ParentSetIterator> AcquireCompositeParentSet(
      std::string_view node_name,
      const fuchsia_driver_index::wire::MatchedCompositeInfo& composite_info);

  async_dispatcher_t* const dispatcher_;
  NodeManager* node_manager_;

  // This stores our parent set collectors that have not completed yet.
  // It is a multimap because each driver url can have multiple parent set collectors.
  // During parent set acquisition, the first one that has an opening is picked.
  std::unordered_multimap<DriverUrl, ParentSetCollector> incomplete_parent_sets_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_MANAGER_H_
