// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_CACHE_H
#define GPU_MAPPING_CACHE_H

#include "gpu_mapping.h"
#include <list>
#include <memory>
#include <unordered_map>

class GpuMappingCache {
public:
    void CacheMapping(std::shared_ptr<GpuMapping> mapping);

    uint64_t memory_footprint() { return memory_footprint_; }

    uint64_t memory_cap() { return memory_cap_; }

    static std::unique_ptr<GpuMappingCache> Create(uint64_t memory_cap = kDefaultMemoryCap);

private:
    GpuMappingCache(uint64_t memory_cap);
    // LRU mapping cache
    using cache_list_t = std::list<std::shared_ptr<GpuMapping>>;
    cache_list_t cache_;
    std::unordered_map<GpuMapping*, cache_list_t::iterator> cache_iterator_map_;

// Memory footprint management
#if defined(MSD_INTEL_ENABLE_MAPPING_CACHE)
    static constexpr uint64_t kDefaultMemoryCap = 512 * 1024 * 1024;
#else
    static constexpr uint64_t kDefaultMemoryCap = 0;
#endif
    uint64_t memory_cap_;
    // Right now this tracks total gpu address space held across all address
    // spaces. TODO(MA-153) make this track total pinned pages held by cache
    uint64_t memory_footprint_ = 0;
};

#endif // GPU_MAPPING_CACHE_H