// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_HASH_CACHE_H_
#define SRC_UI_LIB_ESCHER_UTIL_HASH_CACHE_H_

#include <limits>

#include "src/ui/lib/escher/util/hash.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/util/intrusive_list.h"
#include "src/ui/lib/escher/util/object_pool.h"

#ifndef NDEBUG
#include <lib/syslog/cpp/macros.h>
#endif

namespace escher {

// Base class for items that can be cached in a HashCache.
//
// TODO(fxbug.dev/23918): A fancier implementation would make these fields private.
// Until then, we trust Escher clients to not frob them.
template <typename T>
struct HashCacheItem : public IntrusiveListItem<T> {
 public:
  void set_hash(Hash hash) { hash_ = hash; }
  void set_ring_index(size_t ring_index) { ring_index_ = ring_index; }
  Hash hash() { return hash_; }
  size_t ring_index() const { return ring_index_; }

 private:
  // The hash is stored so that the item can be found in the map when it is
  // found in the ring-to-be-evicted.
  Hash hash_ = {0};

  // Remembers the frame that the item was last used.  If it is used in a
  // different frame, it is moved into that frame's eviction ring.  If it was
  // previously used in the same frame, it doesn't need to move.
  size_t ring_index_ = 0;
};

// Wraps the provided ObjectPool policy so that HashCache clients can focus on
// the domain-specific functionality of cached items, without thinking about
// the HashCache-internal fields of those items.
template <typename T, typename BasePolicyT>
class HashCacheObjectPoolPolicy : public BasePolicyT {
 public:
  template <typename... Args>
  HashCacheObjectPoolPolicy(Args&&... args) : BasePolicyT(std::forward<Args>(args)...) {}

  inline void DestroyPoolObject(T* ptr) {
#ifndef NDEBUG
    FX_DCHECK(ptr->prev == nullptr);
    FX_DCHECK(ptr->next == nullptr);
    FX_DCHECK(ptr->list == nullptr);
#endif
    ptr->set_hash({0});
    ptr->set_ring_index(std::numeric_limits<size_t>::max());
    this->BasePolicyT::DestroyPoolObject(ptr);
  }
};

// Provides a frame-based cache that evicts items that haven't been used for
// |FramesUntilEviction| frames.  In other words:
// - if FramesUntilEviction == 0, there is no frame-to-frame caching; items are
//   evicted even if they were used in the previous frame.
// - if FramesUntilEviction == 1, items are evicted if they were not used in the
//   previous frame, otherwise they remain in the cache.
// - if FramesUntilEviction == 2, items are evicted if two frames pass without
//   the resource being used.
// - and so on...
//
// Clients are responsible for not using the cached object after it has been
// evicted from the cache.  The recommended way to do this is to re-request
// cached objects every frame (or even more often).
template <typename T, typename ObjectPoolPolicyT = DefaultObjectPoolPolicy<T>,
          uint32_t FramesUntilEviction = 4>
class HashCache {
 public:
  template <typename... Args>
  HashCache(Args&&... args) : object_pool_(std::forward<Args>(args)...) {}
  ~HashCache() { Clear(); }

  void Clear() {
    for (size_t i = 0; i <= FramesUntilEviction; ++i) {
      ClearRing(i);
    }
    hash_map_.clear();

    object_pool_.Clear();

    cache_hits_ = 0;
    cache_misses_ = 0;
  }

  void BeginFrame() {
    index_ = (index_ + 1) % (FramesUntilEviction + 1);
    ClearRing(index_);
  }

  std::pair<T*, bool> Obtain(Hash hash) {
    auto it = hash_map_.find(hash);
    if (it != hash_map_.end()) {
      // Item was already cached.
      ++cache_hits_;

      T* item = it->second;
      FX_DCHECK(item->hash() == hash);

      // Move item to the current frame's ring, to prevent it from being flushed
      // from the cache.
      if (item->ring_index() != index_) {
        rings_[index_].MoveToFront(rings_[item->ring_index()], item);
        item->set_ring_index(index_);
      }

      return {item, true};
    } else {
      // The item was not found in the cache, so a new one must be allocated.
      ++cache_misses_;

      T* item = object_pool_.Allocate();
      item->set_hash(hash);
      item->set_ring_index(index_);

      hash_map_.insert(it, std::make_pair(hash, item));
      rings_[index_].InsertFront(item);

      return {item, false};
    }
  }

  size_t cache_hits() const { return cache_hits_; }
  size_t cache_misses() const { return cache_misses_; }
  size_t size() const { return hash_map_.size(); }

  using ObjectPoolPolicyType = HashCacheObjectPoolPolicy<T, ObjectPoolPolicyT>;
  using ObjectPoolType = ObjectPool<T, ObjectPoolPolicyType>;
  const ObjectPoolType& object_pool() const { return object_pool_; }

 private:
  void ClearRing(size_t ring_index) {
    FX_DCHECK(ring_index <= FramesUntilEviction);
    auto& ring = rings_[ring_index];
    while (auto item = ring.PopFront()) {
      hash_map_.erase(item->hash());
      object_pool_.Free(item);
    }
    FX_DCHECK(ring.IsEmpty());
  }

  IntrusiveList<T> rings_[FramesUntilEviction + 1];
  ObjectPoolType object_pool_;
  unsigned index_ = 0;

  HashMap<Hash, T*> hash_map_;
  size_t cache_hits_ = 0;
  size_t cache_misses_ = 0;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_HASH_CACHE_H_
