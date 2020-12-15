// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/allocator/node-reserver.h"

#include <stdint.h>
#include <zircon/types.h>

#include <limits>

#include <bitmap/rle-bitmap.h>

#include "src/storage/blobfs/format.h"

namespace blobfs {

ReservedNode::ReservedNode(NodeReserverInterface* reserver, uint32_t node)
    : reserver_(reserver), node_(node) {}

ReservedNode::ReservedNode(ReservedNode&& o) : reserver_(o.reserver_), node_(o.node_) {
  o.Release();
}

ReservedNode& ReservedNode::operator=(ReservedNode&& o) {
  Reset();
  reserver_ = o.reserver_;
  node_ = o.node_;
  o.Release();
  return *this;
}

ReservedNode::~ReservedNode() { Reset(); }

uint32_t ReservedNode::index() const {
  ZX_DEBUG_ASSERT_MSG(Reserved(), "Accessing unreserved node");
  return node_;
}

void ReservedNode::Reset() {
  if (Reserved()) {
    reserver_->UnreserveNode(std::move(*this));
  }
}

void ReservedNode::Release() { reserver_ = nullptr; }

bool ReservedNode::Reserved() const { return reserver_ != nullptr; }

}  // namespace blobfs
