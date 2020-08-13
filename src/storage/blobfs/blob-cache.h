// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_CACHE_H_
#define SRC_STORAGE_BLOBFS_BLOB_CACHE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <blobfs/cache-policy.h>
#include <digest/digest.h>
#include <fbl/condition_variable.h>
#include <fbl/function.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fs/trace.h>
#include <fs/vnode.h>

#include "cache-node.h"
#include "metrics.h"

namespace blobfs {

using digest::Digest;

// BlobCache contains a collection of weak pointers to vnodes.
//
// This cache also helps manage the lifecycle of these vnodes, controlling what is cached
// when there are no more external references.
//
// Internally, the BlobCache contains a "live set" and "closed set" of vnodes.
// The "live set" contains all Vnodes with a strong reference.
// The "closed set" contains references to Vnodes which are not used, but which exist
// on-disk. These Vnodes may be stored in a "low-memory" state until they are requested.
//
// This class is thread-safe.
class BlobCache {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(BlobCache);
  BlobCache();
  ~BlobCache();

  // Empties the cache, evicting all open nodes and deleting all closed nodes.
  void Reset();

  // Sets the internal cache policy dealing with blob eviction.
  //
  // Refer to the declaration of |CachePolicy| for more information.
  void SetCachePolicy(CachePolicy policy) { cache_policy_ = policy; }

  // Iterates over all non-evicted cached nodes with strong references, invoking |callback| on
  // each one.
  //
  // If a node is inserted into the "live set" via a concurrent call to |Add|, or evicted
  // with a concurrent call to |Evict|, it is undefined if that node will be returned.
  using NextNodeCallback = fbl::Function<void(fbl::RefPtr<CacheNode>)>;
  void ForAllOpenNodes(NextNodeCallback callback);

  // Searches for a blob by |digest|.
  //
  // If a readable blob with the same name exists, it is (optionally) placed in |out|.
  // If no such blob is found, ZX_ERR_NOT_FOUND is returned.
  // |out| may be null. The same error code will be returned as if it was a valid pointer.
  // If |out| is not null, then the returned-by-strong-reference Vnode will exist in the "live
  // set".
  zx_status_t Lookup(const Digest& digest, fbl::RefPtr<CacheNode>* out) __WARN_UNUSED_RESULT;

  // Adds a blob to the "live set" of the cache. If |vnode->ShouldCache()| is true, then
  // this node will remain discoverable using |Lookup()|, even if no strong references
  // remain.
  //
  // Returns ZX_ERR_ALREADY_EXISTS if this blob could not be added because a node with the same
  // key already exists in the cache.
  zx_status_t Add(const fbl::RefPtr<CacheNode>& vnode) __WARN_UNUSED_RESULT;

  // Deletes a blob from the cache.
  // When the last strong reference is removed, it is put into a low-memory state, but
  // not placed into the "closed set" of the cache.
  // Future calls to "Lookup" will not be able to observe this node.
  //
  // Returns ZX_OK if the node was evicted from the cache.
  // Returns ZX_ERR_NOT_FOUND if the node was not in the cache.
  zx_status_t Evict(const fbl::RefPtr<CacheNode>& vnode) __WARN_UNUSED_RESULT;

 private:
  // Resurrects a Vnode with no strong references, and relocate it from the "live set" to the
  // "closed set".
  //
  // Precondition: The blob must have no strong references.
  // This function is currently only safe to call from CacheNode::fbl_recycle.
  void Downgrade(CacheNode* vn);
  friend void CacheNode::fbl_recycle();

  // Identical to |Evict|, but utilizing a raw pointer.
  //
  // This function is only safe to call from:
  // - |Evict|, where the strong reference guarantees that the node will exist in the |open_hash_|
  // or not at all, or
  // - |fbl_recycle|, where the refcount of zero will prevent other nodes from concurrently
  // acquiring a reference. In this case, an argument is passed, identifying that other nodes
  // observing the |open_hash_| via lookup should be signalled if this node is removed.
  zx_status_t EvictUnsafe(CacheNode* vnode, bool from_recycle = false);

  // Returns a strong reference to a node, if it exists. May relocate the
  // node from the |closed_hash_| to the |open_hash_| if no strong references
  // actively exist. |out| must not be nullptr.
  //
  // Returns ZX_OK if the node is found and returned.
  // Returns ZX_ERR_NOT_FOUND if the node doesn't exist in the cache.
  zx_status_t LookupLocked(const uint8_t* key, fbl::RefPtr<CacheNode>* out)
      __TA_REQUIRES(hash_lock_);

  // Upgrades a Vnode which exists in the |closed_hash_| into |open_hash_|,
  // and acquire the strong reference the Vnode which was leaked by
  // |Downgrade()|, if it exists.
  //
  // Precondition: The Vnode must not exist in |open_hash_|.
  fbl::RefPtr<CacheNode> UpgradeLocked(const uint8_t* key) __TA_REQUIRES(hash_lock_);

  // Resets the cache by deleting all members |closed_hash_|.
  void ResetLocked() __TA_REQUIRES(hash_lock_);

  // We need to define this structure to allow the CacheNodes to be indexable by a key
  // which is larger than a primitive type: the keys are 'digest::kSha256Length'
  // bytes long.
  struct MerkleRootTraits {
    static const uint8_t* GetKey(const CacheNode& obj) { return obj.GetKey(); }
    static bool LessThan(const uint8_t* k1, const uint8_t* k2) {
      return memcmp(k1, k2, digest::kSha256Length) < 0;
    }
    static bool EqualTo(const uint8_t* k1, const uint8_t* k2) {
      return memcmp(k1, k2, digest::kSha256Length) == 0;
    }
  };

  // CacheNodes exist in the WAVLTree as long as one or more reference exists;
  // when the Vnode is deleted, it is immediately removed from the WAVL tree.
  using WAVLTreeByMerkle = fbl::WAVLTree<const uint8_t*, CacheNode*, MerkleRootTraits>;

  CachePolicy cache_policy_ = CachePolicy::EvictImmediately;

  fbl::Mutex hash_lock_ = {};
  // All 'in use' blobs.
  WAVLTreeByMerkle open_hash_ __TA_GUARDED(hash_lock_){};
  // All 'closed' blobs.
  WAVLTreeByMerkle closed_hash_ __TA_GUARDED(hash_lock_){};
  // A condition variable which is signalled whenever a CacheNode has been removed from
  // the |open_hash_|. When a CacheNode runs out of references, it exists in the |open_hash_|
  // with no strong references for a short period of time before being removed and
  // either resurrected or destroyed. This means, however, that a concurrent caller
  // trying to |Lookup()| that node may see it, but be unable to acquire it.
  // This variable lets those callers wait until SOME node has been removed from the
  // |open_hash_|, at which point their |Lookup()| may have a different result.
  fbl::ConditionVariable release_cvar_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_CACHE_H_
