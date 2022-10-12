// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/cache_node.h"

#include "src/lib/digest/digest.h"
#include "src/storage/blobfs/blob_cache.h"

namespace blobfs {

CacheNode::CacheNode(fs::PagedVfs& vfs, const digest::Digest& digest,
                     std::optional<CachePolicy> override_cache_policy)
    : fs::PagedVnode(vfs), digest_(digest), overriden_cache_policy_(override_cache_policy) {}

void CacheNode::RecycleNode() {
  if (ShouldCache()) {
    // Migrate from the open cache to the closed cache, keeping the Vnode alive.
    //
    // If the node has already been evicted, it is destroyed.
    GetCache().Downgrade(this);
  } else {
    // Destroy blobs which don't want to be cached.
    //
    // If we're deleting this node, it must not exist in either hash.
    GetCache().EvictUnsafe(this, true);
    ZX_DEBUG_ASSERT(!InContainer());
    delete this;
  }
}

}  // namespace blobfs
