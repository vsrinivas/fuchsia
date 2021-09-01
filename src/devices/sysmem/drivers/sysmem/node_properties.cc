// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node_properties.h"

#include <zircon/assert.h>

#include <vector>

#include "fidl/fuchsia.sysmem2/cpp/wire.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

NodeProperties::~NodeProperties() {
  ZX_DEBUG_ASSERT(child_count() == 0);
  if (node_) {
    logical_buffer_collection_->RemoveCountsForNode(*node_);
    node_->EnsureDetachedFromNodeProperties();
  }
}

// static
std::unique_ptr<NodeProperties> NodeProperties::NewRoot(
    LogicalBufferCollection* logical_buffer_collection) {
  auto result = std::unique_ptr<NodeProperties>(new NodeProperties(logical_buffer_collection));
  ZX_DEBUG_ASSERT(!result->parent_);
  // Set later with SetNode().
  ZX_DEBUG_ASSERT(!result->node_);
  ZX_DEBUG_ASSERT(result->children_.empty());
  return result;
}

NodeProperties* NodeProperties::NewChild(LogicalBufferCollection* logical_buffer_collection) {
  auto result = std::unique_ptr<NodeProperties>(new NodeProperties(logical_buffer_collection));
  auto result_ptr = result.get();
  // The parent Node owns the child Node.
  const auto& [child_iter, inserted] = this->children_.emplace(result_ptr, std::move(result));
  ZX_DEBUG_ASSERT(inserted);
  result_ptr->parent_ = this;
  // Set later with SetNode().
  ZX_DEBUG_ASSERT(!result_ptr->node_);
  ZX_DEBUG_ASSERT(result_ptr->children_.empty());
  // Default to parent's debug info until/unless overriden later (by the client, or by later code
  // that always runs regardless of client behavior).
  //
  // struct copy
  result_ptr->client_debug_info_ = client_debug_info_;
  // Soon we'll do another &= on this mask with the rights_attenuation_mask supplied by the client
  // when creating the child, but the child starts with the same rights masked away as the parent.
  result_ptr->rights_attenuation_mask_ = rights_attenuation_mask_;
  ZX_DEBUG_ASSERT(result_ptr->error_propagation_mode_ == ErrorPropagationMode::kPropagate);
  return result_ptr;
}

// static
std::unique_ptr<NodeProperties> NodeProperties::NewTemporary(
    LogicalBufferCollection* logical_buffer_collection,
    fuchsia_sysmem2::wire::BufferCollectionConstraints buffer_collection_constraints,
    std::string debug_name) {
  auto result = std::unique_ptr<NodeProperties>(new NodeProperties(logical_buffer_collection));
  ZX_DEBUG_ASSERT(!result->parent_);
  // Since temporary, won't ever have a node_.
  ZX_DEBUG_ASSERT(!result->node_);
  ZX_DEBUG_ASSERT(result->children_.empty());
  result->SetBufferCollectionConstraints(TableHolder(logical_buffer_collection->table_set(),
                                                     std::move(buffer_collection_constraints)));
  result->client_debug_info().name = debug_name;
  return result;
}

void NodeProperties::RemoveFromTreeAndDelete() {
  ZX_DEBUG_ASSERT(child_count() == 0);
  // This also deletes "this".
  if (parent_) {
    // Set parent_ to nullptr before "this" is deleted, just in case it makes any use-after-free
    // quicker to track down.
    auto local_parent = parent_;
    parent_ = nullptr;
    local_parent->children_.erase(this);
  } else {
    logical_buffer_collection_->DeleteRoot();
  }
  // "this" is now gone
}

std::vector<NodeProperties*> NodeProperties::BreadthFirstOrder(
    fit::function<NodeFilterResult(const NodeProperties&)> node_filter) {
  std::vector<NodeProperties*> iterate_children_tmp;
  std::vector<NodeProperties*> result;
  NodeFilterResult this_result;
  if (node_filter) {
    this_result = node_filter(*this);
  }
  if (this_result.keep_node) {
    result.push_back(this);
  }
  if (this_result.iterate_children) {
    iterate_children_tmp.push_back(this);
  }
  for (uint32_t i = 0; i < iterate_children_tmp.size(); ++i) {
    NodeProperties* node = iterate_children_tmp[i];
    for (auto& [child_ptr, unused] : node->children_) {
      NodeFilterResult child_result;
      if (node_filter) {
        child_result = node_filter(*child_ptr);
      }
      if (child_result.keep_node) {
        result.push_back(child_ptr);
      }
      if (child_result.iterate_children) {
        iterate_children_tmp.push_back(child_ptr);
      }
    }
  }
  return result;
}

NodeProperties* NodeProperties::parent() const {
  // Can be nullptr if this is the root.
  return parent_;
}

Node* NodeProperties::node() const {
  // Can be nullptr if no Node is owned yet.
  return node_.get();
}

uint32_t NodeProperties::child_count() const { return children_.size(); }

ClientDebugInfo& NodeProperties::client_debug_info() { return client_debug_info_; }

const ClientDebugInfo& NodeProperties::client_debug_info() const { return client_debug_info_; }

uint32_t& NodeProperties::rights_attenuation_mask() { return rights_attenuation_mask_; }

ErrorPropagationMode& NodeProperties::error_propagation_mode() { return error_propagation_mode_; }

const ErrorPropagationMode& NodeProperties::error_propagation_mode() const {
  return error_propagation_mode_;
}

bool NodeProperties::buffers_logically_allocated() const { return buffers_logically_allocated_; }

void NodeProperties::SetBuffersLogicallyAllocated() {
  ZX_DEBUG_ASSERT(!buffers_logically_allocated_);
  buffers_logically_allocated_ = true;
}

bool NodeProperties::has_constraints() const { return !!buffer_collection_constraints_; }

const fuchsia_sysmem2::wire::BufferCollectionConstraints*
NodeProperties::buffer_collection_constraints() const {
  if (!buffer_collection_constraints_) {
    return nullptr;
  }
  return &(**buffer_collection_constraints_);
}

void NodeProperties::SetBufferCollectionConstraints(
    TableHolder<fuchsia_sysmem2::wire::BufferCollectionConstraints> buffer_collection_constraints) {
  ZX_DEBUG_ASSERT(!buffer_collection_constraints_);
  buffer_collection_constraints_.emplace(std::move(buffer_collection_constraints));
}

void NodeProperties::SetNode(fbl::RefPtr<Node> node) {
  // Once a Node is owned, it's ok to switch to a different Node, but not ok to set back to nullptr.
  ZX_DEBUG_ASSERT(node);
  logical_buffer_collection_->AddCountsForNode(*node);
  if (node_) {
    logical_buffer_collection_->RemoveCountsForNode(*node_);
    node_->EnsureDetachedFromNodeProperties();
  }
  node_ = std::move(node);
}

NodeProperties::NodeProperties(LogicalBufferCollection* logical_buffer_collection)
    : logical_buffer_collection_(logical_buffer_collection) {
  ZX_DEBUG_ASSERT(logical_buffer_collection_);
}

uint32_t NodeProperties::node_count() const { return node_count_; }

uint32_t NodeProperties::connected_client_count() const { return connected_client_count_; }

uint32_t NodeProperties::buffer_collection_count() const { return buffer_collection_count_; }

uint32_t NodeProperties::buffer_collection_token_count() const {
  return buffer_collection_token_count_;
}

void NodeProperties::LogInfo(Location location, const char* format, ...) const {
  va_list args;
  va_start(args, format);
  logical_buffer_collection_->VLogClientInfo(location, this, format, args);
  va_end(args);
}

void NodeProperties::LogConstraints(Location location) {
  if (!has_constraints()) {
    LogInfo(FROM_HERE, "No constraints yet.");
    return;
  }
  logical_buffer_collection_->LogConstraints(location, this, *buffer_collection_constraints());
}

}  // namespace sysmem_driver
