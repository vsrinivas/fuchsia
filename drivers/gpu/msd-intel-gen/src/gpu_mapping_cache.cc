// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping_cache.h"
#include "magma_util/dlog.h"
#include "msd_intel_buffer.h"

void GpuMappingCache::AddMapping(std::shared_ptr<GpuMapping> mapping)
{
    DLOG("GpuMappingCache::AddMapping buffer 0x%" PRIx64,
         mapping->buffer()->platform_buffer()->id());

    auto iter = map_.find(mapping.get());
    if (iter == map_.end())
        map_[mapping.get()] = std::move(mapping);
}

void GpuMappingCache::RemoveMapping(std::shared_ptr<GpuMapping> mapping)
{
    DLOG("GpuMappingCache::RemoveMapping buffer 0x%" PRIx64,
         mapping->buffer()->platform_buffer()->id());

    auto iter = map_.find(mapping.get());
    if (iter != map_.end())
        map_.erase(iter);
}

std::unique_ptr<GpuMappingCache> GpuMappingCache::Create()
{
    return std::make_unique<GpuMappingCache>();
}
