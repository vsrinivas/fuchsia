// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::asset::{Asset, AssetId},
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
}

impl Cache {
    pub fn new(capacity: u64) -> Cache {
        Cache { capacity, available: capacity, cache: VecDeque::new() }
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
    fn pop(&mut self) {
        if let Some(asset) = self.cache.pop_front() {
            self.available += asset.buffer.size;
        }
    }

    /// Add a clone of `asset` to the cache, and return the original.
    /// If `asset` is already cached, calls [`move_to_back`] before returning.
    /// If cloning `asset` fails or if `asset` is larger than the cache capacity, nothing is cached.
    /// As many cached [`Assets`] as needed (in LRU order) will be popped to make space for `asset`.
    pub fn push(&mut self, asset: Asset) -> Asset {
        if let Some(index) = self.index_of(asset.id) {
            self.move_to_back(index);
        } else if asset.buffer.size < self.capacity {
            if let Some(clone) = asset.try_clone().ok() {
                while clone.buffer.size > self.available {
                    self.pop();
                }
                self.available -= clone.buffer.size;
                self.cache.push_back(clone);
            }
        }
        asset
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_mem as mem, fuchsia_zircon as zx};

    fn mock_cache() -> Cache {
        Cache {
            capacity: 3000,
            available: 1000,
            cache: VecDeque::from(vec![mock_asset(1, 1024, 1000), mock_asset(2, 1024, 1000)]),
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
        let mut cache = Cache::new(10);
        cache.pop();
        assert_eq!(cache.available, 10);
    }

    #[test]
    fn test_push_new() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let to_push = mock_asset(3, 1024, 1000);
        let cached = cache.push(to_push);
        assert_eq!(cached.id, AssetId(3));
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, unused_before - 1000);
    }

    #[test]
    fn test_push_make_space() {
        let mut cache = mock_cache();
        let to_push = mock_asset(3, 2048, 2000);
        let should_be_popped_id = cache.cache.front().unwrap().id;
        let cached = cache.push(to_push);
        assert_eq!(cached.id, AssetId(3));
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, 0);
        assert!(cache.index_of(should_be_popped_id).is_none());
    }

    #[test]
    fn test_push_cached() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let to_push = cache.cache.front().unwrap().try_clone().unwrap();
        let front_id = to_push.id;
        let cached = cache.push(to_push);
        assert_eq!(cached.id, front_id);
        assert_eq!(cached.id, cache.cache.back().unwrap().id);
        assert_eq!(cache.available, unused_before);
    }

    #[test]
    fn test_push_does_not_fit() {
        let mut cache = mock_cache();
        let unused_before = cache.available;
        let too_big = mock_asset(3, 4096, 4000);
        let too_big = cache.push(too_big);
        assert!(cache.get(too_big.id).is_none());
        assert_eq!(cache.available, unused_before);
    }
}
