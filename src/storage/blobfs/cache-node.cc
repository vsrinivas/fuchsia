// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cache-node.h"

#include <digest/digest.h>

#include "blob-cache.h"

using digest::Digest;

namespace blobfs {

void CacheNode::fbl_recycle() {
  if (ShouldCache()) {
    // Migrate from the open cache to the closed cache, keeping the Vnode alive.
    //
    // If the node has already been evicted, it is destroyed.
    Cache().Downgrade(this);
  } else {
    // Destroy blobs which don't want to be cached.
    //
    // If we're deleting this node, it must not exist in either hash.
    Cache().EvictUnsafe(this, true);
    ZX_DEBUG_ASSERT(!InContainer());
    delete this;
  }
}

CacheNode::CacheNode(const Digest& digest, std::optional<CachePolicy> override_cache_policy)
    : overriden_cache_policy_(override_cache_policy) {
  digest.CopyTo(digest_, sizeof(digest_));
}
CacheNode::~CacheNode() = default;

}  // namespace blobfs
