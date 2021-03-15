// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node.h"

#include "logical_buffer_collection.h"

namespace sysmem_driver {

Node::Node(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
           NodeProperties* node_properties)
    : logical_buffer_collection_(std::move(logical_buffer_collection)),
      node_properties_(node_properties) {}

Node::~Node() {
  // EnsureDetachedFromNodeProperties() should have been called by this point.
  ZX_DEBUG_ASSERT(!node_properties_);
}

LogicalBufferCollection& Node::logical_buffer_collection() const {
  return *logical_buffer_collection_;
}

fbl::RefPtr<LogicalBufferCollection> Node::shared_logical_buffer_collection() {
  return logical_buffer_collection_;
}

NodeProperties& Node::node_properties() const {
  ZX_ASSERT(node_properties_);
  return *node_properties_;
}

void Node::EnsureDetachedFromNodeProperties() { node_properties_ = nullptr; }

}  // namespace sysmem_driver
