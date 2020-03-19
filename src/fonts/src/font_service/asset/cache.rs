// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::asset::{Asset, AssetId},
    fuchsia_inspect::{self as finspect, NumericProperty},
    std::collections::VecDeque,
};

/// An LRU cache for `Buffer`s, bounded by the total size of cached VMOs.
///
/// `capacity` and `available` are [`u64`] instead of [`usize`] for parity with `mem::Buffer.size`.
pub struct Cache {
    /// Maximum allowed sum of `self.buffers[..].size` in bytes.
    capacity: u64,
    /// Bytes available to be used.
    available: u64,
    /// Assets ordered by recency of last use.
    cache: VecDeque<Asset>,

    inspect_data: AssetCacheInspectData,
}

impl Cache {
    /// Creates a new cache instance with the given `capacity` in bytes, and with the given parent
    /// Inspect node.
    pub fn new(capacity: u64, parent_inspect_node: &finspect::Node) -> Cache {
        let inspect_data = AssetCacheInspectData::new(parent_inspect_node, capacity);
        Cache { capacity, available: capacity, cache: VecDeque::new(), inspect_data }
    }

    /// Get the index of the [`Asset`] with ID `id`, if it is cached.
    ///
    /// Runs in `O(self.len)` time, which should be fast enough if the cache is small.
    fn index_of(&self, id: AssetId) -> Option<usize> {
        // Iterate from most- to least-recently used (back to front).
        for (index, asset) in self.cache.iter().enumerate().rev() {
            if asset.id == id {
                return Some(index);
            }
        }
        None
    }

    /// Move the cached [`Asset`] at `index` to the back of the queue (i.e. mark it as
    /// most-recently used) and return a reference to it.
    ///
    /// Returns [`None`] if `index` is out of bounds.
    fn move_to_back(&mut self, index: usize) -> Option<&Asset> {
        if index >= self.cache.len() {
            return None;
        }

        // Don't do anything if this is already at the back.
        if index != self.cache.len() - 1 {
            if let Some(asset) = self.cache.remove(index) {
                self.cache.push_back(asset);
            }
        }
        self.cache.back()
    }

    /// Get a clone of the cached [`Asset`] with ID `id`.
    /// Returns [`None`] if cloning fails or the requested [`Asset`] is not cached.
    pub fn get(&mut self, id: AssetId) -> Option<Asset> {
        self.index_of(id)
            .and_then(move |index| self.move_to_back(index))
            .and_then(|cached| cached.try_clone().ok())
    }

    /// Remove and return the least recently used cached [`Asset`].
    /// Returns [`None`] if the cache is empty.
    fn pop(&mut self) -> Option<Asset> {
        let popped = self.cache.pop_front();
        if let Some(asset) = &popped {
            self.available += asset.buffer.size;
            self.inspect_data.on_pop(&asset);
        }
        popped
    }

    /// Add a clone of `asset` to the cache, and return the original.
    /// If `asset` is already cached, calls [`move_to_back`] before returning.
    /// If cloning `asset` fails or if `asset` is larger than the cache capacity, nothing is cached.
    /// As many cached [`Assets`] as needed (in LRU order) will be popped to make space for `asset`.
    ///
    /// Returns the original given `Asset`, a `bool` indicating whether it is now cached
    /// successfully (including if it was already cached), and any `Asset`s that were evicted to
    /// make room for it.
    pub fn push(&mut self, asset: Asset) -> (Asset, bool, Vec<Asset>) {
        let mut is_cached = false;
        let mut evicted = Vec::<Asset>::new();

        if let Some(index) = self.index_of(asset.id) {
            self.move_to_back(index);
            is_cached = true
        } else if asset.buffer.size < self.capacity {
            if let Some(clone) = asset.try_clone().ok() {
                while clone.buffer.size > self.available {
                    if let Some(popped) = self.pop() {
                        evicted.push(popped);
                    }
                }
                is_cached = true;
                self.available -= clone.buffer.size;
                self.inspect_data.on_push(&clone);
                self.cache.push_back(clone);
            }
        }
        (asset, is_cached, evicted)
    }
}

/// Inspect data for [AssetCache].
#[allow(dead_code)]
pub struct AssetCacheInspectData {
    /// Root Inspect node for the cache
    node: finspect::Node,
    /// Tracks bytes used by the cache
    used_bytes: finspect::UintProperty,
    /// Tracks number of assets in the cache
    count: finspect::UintProperty,
}

impl AssetCacheInspectData {
    /// Creates a new instance with the given parent node and cache capacity.
    fn new(parent_node: &finspect::Node, capacity: u64) -> Self {
        let node = parent_node.create_child("asset_cache");
        node.record_uint("capacity_bytes", capacity);
        let used = node.create_uint("used_bytes", 0);
        let count = node.create_uint("count", 0);
        AssetCacheInspectData { node, used_bytes: used, count }
    }

    fn on_pop(&mut self, popped_asset: &Asset) {
        self.used_bytes.subtract(popped_asset.buffer.size);
        self.count.subtract(1);
    }

    fn on_push(&mut self, pushed_asset: &Asset) {
        self.used_bytes.add(pushed_asset.buffer.size);
        self.count.add(1);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_mem as mem, fuchsia_inspect::assert_inspect_tree,
        fuchsia_zircon as zx,
    };

    /// Creates a `Cache` with some mocked assets.
    fn mock_cache() -> Cache {
        let inspector = finspect::Inspector::new();
        mock_cache_with_inspector(&inspector)
    }

    /// Creates a cache with some mocked assets and the given `Inspector`.
    fn mock_cache_with_inspector(inspector: &finspect::Inspector) -> Cache {
        let inspector_root = inspector.root();
        let capacity = 3000;
        Cache {
            capacity,
            available: 1000,
            cache: VecDeque::from(vec![mock_asset(1, 1024, 1000), mock_asset(2, 1024, 1000)]),
            inspect_data: AssetCacheInspectData::new(inspector_root, capacity),
        }
    }

    fn mock_asset(id: u32, vmo_size: u64, buf_size: u64) -> Asset {
        assert!(vmo_size > buf_size);
        Asset {
            id: AssetId(id),
            buffer: mem::Buffer { vmo: zx::Vmo::create(vmo_size).unwrap(), size: buf_size },
        }
    }

    #[test]
    fn test_index_of_hit() {
        let cache = mock_cache();
        assert_eq!(cache.index_of(AssetId(1)), Some(0));
        assert_eq!(cache.index_of(AssetId(2)), Some(1));
    }

    #[test]
    fn test_index_of_miss() {
        let cache = mock_cache();
        assert!(cache.index_of(AssetId(0)).is_none());
    }

    #[test]
    fn test_move_to_back() {
        let mut cache = mock_cache();
        let front_id = cache.cache.front().unwrap().id;
        let moved = cache.move_to_back(0).unwrap();
        assert_eq!(moved.id, front_id);
        assert_ne!(cache.cache.front().unwrap().id, front_id);
    }

    #[test]
    fn test_move_to_back_out_of_bounds() {
        let mut cache = mock_cache();
        assert!(cache.move_to_back(3).is_none());
    }

    #[test]
    fn test_get_hit() {
        let mut cache = mock_cache();
        let cached = cache.get(AssetId(1)).unwrap();
        assert_eq!(cache.cache.back().unwrap().id, cached.id);
    }

    #[test]
    fn test_get_miss() {
        let mut cache = mock_cache();
        let should_be_none = cache.get(AssetId(3));
        assert!(should_be_none.is_none());
    }

    #[test]
    fn test_pop() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let should_be_popped_id = cache.cache.front().unwrap().id;
        let should_be_popped_size = cache.cache.front().unwrap().buffer.size;
        cache.pop();
        assert!(cache.index_of(should_be_popped_id).is_none());
        assert_eq!(cache.available, unused_before + should_be_popped_size);
    }

    #[test]
    fn test_pop_empty() {
        let inspector = finspect::Inspector::new();
        let mut cache = Cache::new(10, inspector.root());
        cache.pop();
        assert_eq!(cache.available, 10);
    }

    #[test]
    fn test_push_new() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let to_push = mock_asset(3, 1024, 1000);
        let (cached, is_cached, evicted) = cache.push(to_push);
        assert_eq!(cached.id, AssetId(3));
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, unused_before - 1000);
        assert!(is_cached);
        assert!(evicted.is_empty());
    }

    #[test]
    fn test_push_make_space() {
        let mut cache = mock_cache();
        let to_push = mock_asset(3, 2048, 2000);
        let should_be_popped_id = cache.cache.front().unwrap().id;
        let (cached, is_cached, evicted) = cache.push(to_push);
        assert_eq!(cached.id, AssetId(3));
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, 0);
        assert!(cache.index_of(should_be_popped_id).is_none());
        assert!(is_cached);
        assert_eq!(evicted.len(), 1);
        assert_eq!(evicted[0].id, should_be_popped_id);
    }

    #[test]
    fn test_push_cached() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let to_push = cache.cache.front().unwrap().try_clone().unwrap();
        let front_id = to_push.id;
        let (cached, is_cached, evicted) = cache.push(to_push);
        assert_eq!(cached.id, front_id);
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, unused_before);
        assert!(is_cached);
        assert!(evicted.is_empty());
    }

    #[test]
    fn test_push_does_not_fit() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let too_big = mock_asset(3, 4096, 4000);
        let (too_big, is_cached, evicted) = cache.push(too_big);
        assert!(!is_cached);
        assert!(evicted.is_empty());
        assert!(cache.get(too_big.id).is_none());
        assert_eq!(cache.available, unused_before);
    }

    #[test]
    fn test_inspect_data() {
        let inspector = finspect::Inspector::new();
        let capacity = 3000;
        let mut cache = Cache::new(capacity, inspector.root());
        assert_inspect_tree!(inspector, root: {
            asset_cache: {
                capacity_bytes: 3000u64,
                used_bytes: 0u64,
                count: 0u64,
            }
        });

        let asset = mock_asset(1, 1024, 1000);
        cache.push(asset);

        assert_inspect_tree!(inspector, root: contains {
            asset_cache: {
                capacity_bytes: 3000u64,
                used_bytes: 1000u64,
                count: 1u64,
            }
        });
    }
}
