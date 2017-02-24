// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping_cache.h"
#include "magma_util/dlog.h"
#include "msd_intel_buffer.h"

GpuMappingCache::GpuMappingCache(uint64_t memory_cap) : memory_cap_(memory_cap)
{
    DLOG("Creating new global mapping cache of size 0x%lx", memory_cap_);
}

void GpuMappingCache::CacheMapping(std::shared_ptr<GpuMapping> mapping)
{
    DLOG("GpuMappingCache::CacheMapping");

    // Look to see if the mapping is already in the cache
    auto meta_iter = cache_iterator_map_.find(mapping.get());
    if (meta_iter != cache_iterator_map_.end()) {
        // Mapping is already in the cache, move it to the front
        auto cache_iter = meta_iter->second;
        cache_.erase(cache_iter);
        cache_.push_front(mapping);

        // Update cache_iterator_map_ to hold the new iterator
        cache_iterator_map_.erase(meta_iter);
        cache_iterator_map_.insert(std::make_pair(mapping.get(), cache_.begin()));
    } else {
        // Mapping is not in the cache, so add it
        if (mapping->length() > memory_cap_) {
            DLOG("attempting to cache mapping of size %lu bytes but cache is only %lu bytes, "
                 "ignoring",
                 mapping->length(), memory_cap_);
            return;
        }

        cache_.push_front(mapping);
        cache_iterator_map_.insert(std::make_pair(mapping.get(), cache_.begin()));

        // Adjust memory footprint to account for new mapping
        memory_footprint_ += mapping->length();

        // Purge LRU entries from the cache until were back under the cap
        while (memory_footprint_ > memory_cap_) {
            std::shared_ptr<GpuMapping> purged_mapping = cache_.back();

            meta_iter = cache_iterator_map_.find(purged_mapping.get());
            DASSERT(meta_iter != cache_iterator_map_.end());
            cache_iterator_map_.erase(meta_iter);

            cache_.pop_back();
            memory_footprint_ -= purged_mapping->length();
        }

        DLOG("inserted new entry of offset 0x%lx, length 0x%lx, buffer id 0x%lx new "
             "footprint %lu bytes",
             mapping->offset(), mapping->length(), mapping->buffer()->platform_buffer()->id(),
             memory_footprint_);
    }
}

std::unique_ptr<GpuMappingCache> GpuMappingCache::Create(uint64_t memory_cap)
{
    return std::unique_ptr<GpuMappingCache>(new GpuMappingCache(memory_cap));
}
