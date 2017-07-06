// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_CACHE_H
#define GPU_MAPPING_CACHE_H

#include "gpu_mapping.h"
#include <unordered_map>

class GpuMappingCache {
public:
    static std::unique_ptr<GpuMappingCache> Create();

    void AddMapping(std::shared_ptr<GpuMapping> mapping);
    void RemoveMapping(std::shared_ptr<GpuMapping> mapping);

    uint64_t mapping_count() { return map_.size(); }

private:
    std::unordered_map<GpuMapping*, std::shared_ptr<GpuMapping>> map_;
};

#endif // GPU_MAPPING_CACHE_H