// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_CACHE_NODE_H_
#define SRC_STORAGE_BLOBFS_CACHE_NODE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <digest/digest.h>
#include <fbl/function.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fs/vnode.h>

namespace blobfs {

using digest::Digest;

// Forward declared because CacheNode needs a mechanism for accessing the class
// when it runs out of strong references.
class BlobCache;

// An abstract blob-backed Vnode, which is managed by the BlobCache.
class CacheNode : public fs::Vnode,
                  private fbl::Recyclable<CacheNode>,
                  public fbl::WAVLTreeContainable<CacheNode*> {
 public:
  explicit CacheNode(const Digest& digest);
  virtual ~CacheNode();

  // Invoked by fbl::RefPtr when all strong references to RefPtr<CacheNode> go out of scope.
  //
  // If a derived class wishes to participate in the Cache's lifetime management,
  // they must implement the following:
  //
  // void fbl_recycle() final {
  //     CacheNode::fbl_recycle();
  // }
  void fbl_recycle() override;

  // Returns a reference to the BlobCache.
  //
  // The BlobCache must outlive all CacheNodes; this method is invoked from the recycler
  // of a CacheNode.
  //
  // The implementation of this method must not invoke any other CacheNode methods.
  // The implementation of this method must not attempt to acquire a reference to |this|.
  virtual BlobCache& Cache() = 0;

  // Identifies if the node should be recycled when it is terminated,
  // keeping it cached (although possibly in a reduced state).
  //
  // This should be true as long as the blob exists on persistent storage, and
  // would be visible again on reboot.
  //
  // The implementation of this method must not invoke any other CacheNode methods.
  // The implementation of this method must not attempt to acquire a reference to |this|.
  virtual bool ShouldCache() const = 0;

  // Places the Vnode into a low-memory state. This function may be invoked when
  // migrating the node from a "live cache" to a "closed cache".
  //
  // The implementation of this method must not invoke any other CacheNode methods.
  // The implementation of this method must not attempt to acquire a reference to |this|.
  virtual void ActivateLowMemory() = 0;

  // Returns the node's digest.
  const uint8_t* GetKey() const { return &digest_[0]; }
  digest::Digest GetKeyAsDigest() const { return digest::Digest(digest_); }

 private:
  uint8_t digest_[digest::kSha256Length] = {};
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_CACHE_NODE_H_
