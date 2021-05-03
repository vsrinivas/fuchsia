// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_cache.h"

#include <zircon/assert.h>
#include <zircon/status.h>

#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "src/lib/digest/digest.h"
#include "src/lib/storage/vfs/cpp/trace.h"

using digest::Digest;

namespace blobfs {

BlobCache::BlobCache() = default;

BlobCache::~BlobCache() {
  if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    // There shouldn't be any outstanding strong references to |CacheNode|s when the |BlobCache| is
    // destroyed. |CacheNode|s call into |BlobCache| when they lose their last strong reference but
    // aren't notified when the |BlobCache| is destroyed and can cause a use after free bug.
    fbl::AutoLock lock(&hash_lock_);
    ZX_DEBUG_ASSERT(open_hash_.is_empty());
  }
  Reset();
}

void BlobCache::Reset() {
  ForAllOpenNodes([this](fbl::RefPtr<CacheNode> node) {
    // If someone races alongside Reset, and evicts an open node concurrently with us,
    // a status other than "ZX_OK" may be returned. This is allowed.
    __UNUSED zx_status_t status = Evict(node);
    return ZX_OK;
  });

  fbl::AutoLock lock(&hash_lock_);
  ResetLocked();
}

void BlobCache::ResetLocked() {
  // All nodes in closed_hash_ have been leaked. If we're attempting to reset the
  // cache, these nodes must be explicitly deleted.
  CacheNode* node = nullptr;
  while ((node = closed_hash_.pop_front()) != nullptr) {
    delete node;
  }
}

zx_status_t BlobCache::ForAllOpenNodes(NextNodeCallback callback) {
  fbl::RefPtr<CacheNode> old_vnode = nullptr;
  fbl::RefPtr<CacheNode> vnode = nullptr;

  while (true) {
    // Scope the lock to prevent letting fbl::RefPtr<CacheNode> destructors from running while
    // it is held.
    {
      fbl::AutoLock lock(&hash_lock_);
      if (open_hash_.is_empty()) {
        return ZX_OK;
      }

      CacheNode* raw_vnode = nullptr;
      if (old_vnode == nullptr) {
        // Acquire the first node from the front of the cache...
        raw_vnode = &open_hash_.front();
      } else {
        // ... Acquire all subsequent nodes by iterating from the lower bound of the current node.
        auto current = open_hash_.lower_bound(old_vnode->digest());
        if (current == open_hash_.end()) {
          return ZX_OK;
        } else if (current.CopyPointer() != old_vnode.get()) {
          raw_vnode = current.CopyPointer();
        } else {
          auto next = ++current;
          if (next == open_hash_.end()) {
            return ZX_OK;
          }
          raw_vnode = next.CopyPointer();
        }
      }
      vnode = fbl::MakeRefPtrUpgradeFromRaw(raw_vnode, hash_lock_);
      if (vnode == nullptr) {
        // The vnode is actively being deleted. Ignore it.
        release_cvar_.Wait(&hash_lock_);
        continue;
      }
    }
    zx_status_t status = callback(vnode);
    old_vnode = std::move(vnode);
    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_ERR_NEXT && status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t BlobCache::Lookup(const Digest& digest, fbl::RefPtr<CacheNode>* out) {
  TRACE_DURATION("blobfs", "BlobCache::Lookup");

  // Look up the blob in the maps.
  fbl::RefPtr<CacheNode> vnode = nullptr;
  // Avoid releasing a reference to |vnode| while holding |hash_lock_|.
  {
    fbl::AutoLock lock(&hash_lock_);
    zx_status_t status = LookupLocked(digest, &vnode);
    if (status != ZX_OK) {
      return status;
    }
  }
  ZX_DEBUG_ASSERT(vnode != nullptr);

  if (out != nullptr) {
    *out = std::move(vnode);
  }
  return ZX_OK;
}

zx_status_t BlobCache::LookupLocked(const digest::Digest& key, fbl::RefPtr<CacheNode>* out) {
  ZX_DEBUG_ASSERT(out != nullptr);

  // Try to acquire the node from the open hash, if possible.
  while (true) {
    auto raw_vnode = open_hash_.find(key).CopyPointer();
    if (raw_vnode != nullptr) {
      *out = fbl::MakeRefPtrUpgradeFromRaw(raw_vnode, hash_lock_);
      if (*out == nullptr) {
        // This condition is only possible if:
        // - The raw pointer to the Vnode exists in the open map, with refcount == 0.
        // - Another thread is fbl_recycling this Vnode, but has not yet resurrected/evicted it.
        // - The vnode is being moved to the close cache, and is not yet purged.
        //
        // It is not safe for us to attempt to Resurrect the Vnode. If we do so, then the caller of
        // Lookup may unlink, purge, and destroy the Vnode concurrently before the original caller
        // of "fbl_recycle" completes.
        //
        // Since the window of time for this condition is extremely small (between Release and the
        // resurrection of the Vnode), and only contains a single flag check, we use a condition
        // variable to wait until it is released, and try again.
        release_cvar_.Wait(&hash_lock_);
        continue;
      }
      return ZX_OK;
    }
    break;
  }

  // If the node doesn't exist in the open hash, acquire it from the closed hash.
  *out = UpgradeLocked(key);
  if (*out == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

zx_status_t BlobCache::Add(const fbl::RefPtr<CacheNode>& vnode) {
  TRACE_DURATION("blobfs", "BlobCache::Add");

  // Avoid running the old_node destructor while holding the lock.
  fbl::RefPtr<CacheNode> old_node;
  {
    fbl::AutoLock lock(&hash_lock_);
    if (LookupLocked(vnode->digest(), &old_node) == ZX_OK) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    open_hash_.insert(vnode.get());
  }
  return ZX_OK;
}

zx_status_t BlobCache::Evict(const fbl::RefPtr<CacheNode>& vnode) {
  TRACE_DURATION("blobfs", "BlobCache::Evict");

  return EvictUnsafe(vnode.get());
}

zx_status_t BlobCache::EvictUnsafe(CacheNode* vnode, bool from_recycle) {
  fbl::AutoLock lock(&hash_lock_);

  // If this node isn't in any container, we have nothing to evict.
  if (!vnode->InContainer()) {
    return ZX_ERR_NOT_FOUND;
  }

  ZX_ASSERT_MSG(open_hash_.erase(*vnode) != nullptr, "Vnode not present in the open hashmap.");
  ZX_ASSERT(closed_hash_.find(vnode->digest()).CopyPointer() == nullptr);
  ZX_ASSERT_MSG((closed_hash_.find(vnode->digest()).CopyPointer()) == nullptr,
                "Vnode present in closed hashmap.");

  // If we successfully evicted the node from a container, we may have been invoked from
  // fbl_recycle. In this case, a caller to |Lookup| may be blocked waiting until this "open node"
  // is evicted.
  //
  // For this reason, they should be signalled.
  if (from_recycle) {
    release_cvar_.Broadcast();
  }
  return ZX_OK;
}

void BlobCache::Downgrade(CacheNode* raw_vnode) {
  fbl::AutoLock lock(&hash_lock_);
  // We must resurrect the vnode while holding the lock to prevent it from being concurrently
  // accessed in Lookup, and gaining a strong reference before being erased from open_hash_.
  raw_vnode->ResurrectRef();
  fbl::RefPtr<CacheNode> vnode = fbl::ImportFromRawPtr(raw_vnode);

  // If the node has already been evicted, destroy it instead of caching.
  //
  // Delete it explicitly to prevent repeatedly calling fbl_recycle.
  if (!vnode->InContainer()) {
    delete fbl::ExportToRawPtr(&vnode);
    return;
  }

  ZX_ASSERT_MSG(open_hash_.erase(*raw_vnode) != nullptr, "Vnode present in open hash.");
  release_cvar_.Broadcast();
  ZX_ASSERT_MSG(closed_hash_.insert_or_find(vnode.get()), "Vnode absent in closed hashmap.");

  CachePolicy policy = vnode->overriden_cache_policy().value_or(cache_policy_);
  // While in the closed cache, the blob may either be destroyed or in an inactive state. The
  // toggles here make tradeoffs between memory usage and performance.
  switch (policy) {
    case CachePolicy::EvictImmediately:
      vnode->ActivateLowMemory();
      break;
    case CachePolicy::NeverEvict:
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected cache policy");
  }

  // To exist in the closed_hash_, this RefPtr must be leaked. See the complement of this leak in
  // UpgradeLocked.
  __UNUSED auto leak = fbl::ExportToRawPtr(&vnode);
}

fbl::RefPtr<CacheNode> BlobCache::UpgradeLocked(const digest::Digest& key) {
  ZX_DEBUG_ASSERT(open_hash_.find(key).CopyPointer() == nullptr);
  CacheNode* raw_vnode = closed_hash_.erase(key);
  if (raw_vnode == nullptr) {
    return nullptr;
  }
  open_hash_.insert(raw_vnode);
  // To have existed in the closed_hash_, this RefPtr must have been leaked. See the complement of
  // this adoption in Downgrade.
  return fbl::ImportFromRawPtr(raw_vnode);
}

}  // namespace blobfs
