// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_CACHE_NODE_H_
#define SRC_STORAGE_BLOBFS_CACHE_NODE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/fit/function.h>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>

#include "src/lib/digest/digest.h"
#include "src/lib/storage/vfs/cpp/paged_vnode.h"
#include "src/storage/blobfs/cache_policy.h"

namespace blobfs {

using digest::Digest;

// Forward declared because CacheNode needs a mechanism for accessing the class when it runs out of
// strong references.
class BlobCache;

// An abstract blob-backed Vnode, which is managed by the BlobCache.
class CacheNode : public fs::PagedVnode,
                  private fbl::Recyclable<CacheNode>,
                  public fbl::WAVLTreeContainable<CacheNode*> {
 public:
  explicit CacheNode(fs::PagedVfs& vfs, const Digest& digest,
                     std::optional<CachePolicy> override_cache_policy = std::nullopt);
  virtual ~CacheNode() = default;

  const Digest& digest() const { return digest_; }

  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

  // Returns a reference to the BlobCache.
  //
  // The BlobCache must outlive all CacheNodes; this method is invoked from the recycler of a
  // CacheNode.
  //
  // The implementation of this method must not invoke any other CacheNode methods. The
  // implementation of this method must not attempt to acquire a reference to |this|.
  virtual BlobCache& GetCache() = 0;

  // Identifies if the node should be recycled when it is terminated, keeping it cached (although
  // possibly in a reduced state).
  //
  // This should be true as long as the blob exists on persistent storage, and would be visible
  // again on reboot.
  //
  // The implementation of this method must not invoke any other CacheNode methods. The
  // implementation of this method must not attempt to acquire a reference to |this|.
  virtual bool ShouldCache() const = 0;

  // Places the Vnode into a low-memory state. This function may be invoked when migrating the node
  // from a "live cache" to a "closed cache".
  //
  // The implementation of this method must not invoke any other CacheNode methods. The
  // implementation of this method must not attempt to acquire a reference to |this|.
  virtual void ActivateLowMemory() = 0;

  // If the node should have a specific cache discipline, this method returns it. Otherwise, the
  // system-wide policy is applied.
  std::optional<CachePolicy> overriden_cache_policy() const { return overriden_cache_policy_; }
  void set_overridden_cache_policy(CachePolicy policy) { overriden_cache_policy_ = policy; }

 protected:
  // Vnode memory management function called when the reference count reaches 0.
  void RecycleNode() override;

 private:
  digest::Digest digest_;
  std::optional<CachePolicy> overriden_cache_policy_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_CACHE_NODE_H_
