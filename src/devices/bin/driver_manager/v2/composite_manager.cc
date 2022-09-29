// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/composite_manager.h"

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;

namespace dfv2 {

namespace {

struct ValidatedCompositeInfo {
  uint32_t node_index;
  uint32_t num_nodes;
  std::string url;
};

zx::status<ValidatedCompositeInfo> GetValidatedCompositeInfo(
    std::string_view node_name,
    const fuchsia_driver_index::wire::MatchedCompositeInfo& composite_info) {
  if (!composite_info.has_node_index() || !composite_info.has_num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', missing fields for composite driver", node_name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (composite_info.node_index() >= composite_info.num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', the node index is out of range", node_name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (!composite_info.has_driver_info() || !composite_info.driver_info().has_url()) {
    LOGF(ERROR, "Failed to match Node '%s', missing driver info fields for composite driver",
         node_name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(
      ValidatedCompositeInfo{.node_index = composite_info.node_index(),
                             .num_nodes = composite_info.num_nodes(),
                             .url = std::string(composite_info.driver_info().url().get())});
}

}  // namespace

CompositeNodeManager::CompositeNodeManager(async_dispatcher_t* dispatcher,
                                           NodeManager* driver_binder)
    : dispatcher_(dispatcher), node_manager_(driver_binder) {}

zx::status<CompositeNodeManager::ParentSetIterator> CompositeNodeManager::AcquireCompositeParentSet(
    std::string_view node_name, const fdi::wire::MatchedCompositeInfo& composite_info) {
  // Ensure that the composite_info has all necessary fields on it.
  auto validated_result = GetValidatedCompositeInfo(node_name, composite_info);
  if (validated_result.is_error()) {
    return validated_result.take_error();
  }

  auto& valid_composite_info = validated_result.value();

  // Check if there are existing composite parent sets for the composite driver.
  // This is done by checking if any of the existing parent sets for the driver url have room
  // for the `composite_info`.
  for (auto [it, end] = incomplete_parent_sets_.equal_range(valid_composite_info.url); it != end;
       it++) {
    auto& [_, parent_set] = *it;
    ZX_ASSERT(parent_set.size() == valid_composite_info.num_nodes);

    // If the iterator is pointing to one that doesn't contain the composite_info, we have acquired
    // the parent set that the node should be added to.
    if (!parent_set.ContainsNode(valid_composite_info.node_index)) {
      return zx::ok(it);
    }
  }

  // No composite parent set exist for the composite driver, create a new one and return it.
  return zx::ok(incomplete_parent_sets_.emplace(std::move(valid_composite_info.url),
                                                valid_composite_info.num_nodes));
}

zx::status<Node*> CompositeNodeManager::HandleMatchedCompositeInfo(
    Node& node, const fuchsia_driver_index::wire::MatchedCompositeInfo& matched_driver) {
  auto parent_set_result = AcquireCompositeParentSet(node.name(), matched_driver);
  if (parent_set_result.is_error()) {
    return parent_set_result.take_error();
  }
  auto& [_, parent_set] = *parent_set_result.value();

  // Add the node to the parent set that was acquired for it.
  parent_set.AddNode(matched_driver.node_index(), node.weak_from_this());

  // Check if we have all the nodes for the composite driver.
  auto completed_parents = parent_set.GetIfComplete();
  if (!completed_parents.has_value()) {
    // Parent set is not complete yet.
    return zx::error(ZX_ERR_NEXT);
  }

  // Parent set is complete, remove it from our incomplete_parent_sets_.
  incomplete_parent_sets_.erase(*parent_set_result);

  // Create a composite node for the composite driver with our complete parent set.
  std::vector<std::string> parents_names;
  for (auto name : matched_driver.node_names()) {
    parents_names.emplace_back(name.get());
  }
  auto composite = Node::CreateCompositeNode(
      matched_driver.composite_name().get(), std::move(*completed_parents),
      std::move(parents_names), {}, node_manager_, dispatcher_);
  if (composite.is_error()) {
    return composite.take_error();
  }

  // We can return a pointer, as the composite node is owned by its parents.
  return zx::ok(composite.value().get());
}

void CompositeNodeManager::Inspect(inspect::Inspector& inspector, inspect::Node& root) const {
  for (auto& [url, parent_set] : incomplete_parent_sets_) {
    auto child = root.CreateChild(url);
    for (uint32_t i = 0; i < parent_set.size(); i++) {
      auto& node = parent_set.get(i);
      if (auto real = node.lock()) {
        child.CreateString(std::string("parent-").append(std::to_string(i)), real->TopoName(),
                           &inspector);
      } else {
        child.CreateString(std::string("parent-").append(std::to_string(i)), "<empty>", &inspector);
      }
    }
    inspector.emplace(std::move(child));
  }
}

}  // namespace dfv2
