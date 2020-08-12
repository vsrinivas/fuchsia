// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/buffer_cache.h"

#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {

// The maximum ratio that the allocated buffer can exceed the requested size.
// Note: If there are large discrepancies between requested size and cached
// buffer size, it would make sense to sub-allocate a buffer and release a
// smaller buffer portion. For now, the cache will reuse a buffer that is at
// most 2x the size requested.
constexpr int8_t kMaxBufferAllocationRequestRatio = 2;

BufferCache::BufferCache(EscherWeakPtr escher)
    : ResourceRecycler(escher),
      cache_size_(0),
      gpu_allocator_(escher->gpu_allocator()->GetWeakPtr()),
      weak_factory_(this) {
  FX_DCHECK(free_buffer_cache_.empty());
  FX_DCHECK(free_buffers_.empty());
}

BufferCache::~BufferCache() {
  free_buffer_cache_.clear();
  free_buffers_by_id_.clear();
  cache_size_ = 0;
  free_buffers_.clear();
}

BufferPtr BufferCache::NewHostBuffer(vk::DeviceSize vk_size) {
  TRACE_DURATION("gfx", "escher::BufferCache::NewHostBuffer");
  BufferPtr buffer;
  // See if there's a buffer of the right size. Or, find the smallest buffer
  // that is big enough to handle the size request.
  auto size_itr = free_buffers_.lower_bound(vk_size);

  if (size_itr != free_buffers_.end() &&
      size_itr->first > kMaxBufferAllocationRequestRatio * vk_size) {
    // Next largest buffer is too big, just create a new one.
    size_itr = free_buffers_.end();
  }

  if (size_itr != free_buffers_.end()) {
    // There is a free buffer in the cache that can be recycled.
    auto& buffer_list_at_size = size_itr->second;
    auto buf = std::move(buffer_list_at_size.front());
    buffer = BufferPtr(buf.release());

    // Remove the element from the free map, and prune the map if necessary.
    buffer_list_at_size.pop_front();
    if (buffer_list_at_size.empty()) {
      free_buffers_.erase(size_itr);
    }

    // Remove the buffer from the cache.
    cache_size_ -= buffer->size();
    auto info_itr = free_buffers_by_id_.find(buffer->uid());
    FX_DCHECK(info_itr != free_buffers_by_id_.end());
    auto time_key = info_itr->second.allocation_time;
    free_buffer_cache_.erase(free_buffer_cache_.find(time_key));
    free_buffers_by_id_.erase(info_itr);
  } else {
    // Construct a new buffer of the requested size.
    buffer = gpu_allocator_->AllocateBuffer(this, vk_size, kUsageFlags, kMemoryPropertyFlags);
  }

  return buffer;
}

void BufferCache::RecycleResource(std::unique_ptr<Resource> resource) {
  FX_DCHECK(resource->IsKindOf<Buffer>());

  std::unique_ptr<Buffer> buffer(static_cast<Buffer*>(resource.release()));
  CacheInfo cache_info;
  cache_info.id = buffer->uid();
  cache_info.allocation_time = std::chrono::steady_clock::now();
  // TODO(fxbug.dev/40736): Now buffer->size() is the size of VkBuffer, so it can only
  // reclaim buffers of size greater than or equal to the requested size. For
  // buffers with size less than the requested size, but with memory enough to
  // hold the requested buffer, it doesn't work.
  cache_info.size = buffer->size();
  // Ensure this buffer is not already tracked.
  FX_DCHECK(free_buffers_by_id_.find(cache_info.id) == free_buffers_by_id_.end())
      << "uid: " << buffer->uid();

  // Add to the map.
  free_buffers_[cache_info.size].emplace_back(std::move(buffer));

  // Add to the cache.
  cache_size_ += cache_info.size;
  free_buffer_cache_[cache_info.allocation_time] = cache_info;
  free_buffers_by_id_[cache_info.id] = cache_info;

  // Prune if the cache has grown too much.
  while (cache_size_ > kMaxMemoryCached && !free_buffer_cache_.empty()) {
    // Find the oldest buffer in the cache.
    auto cache_itr = free_buffer_cache_.lower_bound(std::chrono::steady_clock::time_point::min());
    FX_DCHECK(cache_itr != free_buffer_cache_.end());
    uint64_t id_to_free = cache_itr->second.id;
    vk::DeviceSize size_freed = cache_itr->second.size;

    // Drop the entries from the cache.
    free_buffer_cache_.erase(cache_itr);
    free_buffers_by_id_.erase(free_buffers_by_id_.find(id_to_free));
    cache_size_ -= size_freed;
    FX_DCHECK(cache_size_ >= 0);

    // Remove the buffer from the free map, releasing the buffer.
    auto find_map_for_size_itr = free_buffers_.find(size_freed);
    FX_DCHECK(find_map_for_size_itr != free_buffers_.end());

    auto& buffer_list_at_size = find_map_for_size_itr->second;
    auto find_buf_itr = std::find_if(buffer_list_at_size.begin(), buffer_list_at_size.end(),
                                     [id_to_free](const std::unique_ptr<Buffer>& buffer) {
                                       return buffer->uid() == id_to_free;
                                     });
    FX_DCHECK(find_buf_itr != buffer_list_at_size.end());
    // Release the buffer.
    buffer_list_at_size.erase(find_buf_itr);
    // If there's no other buffers of this size, release size map from the
    // free_buffers_ map.
    if (buffer_list_at_size.empty()) {
      free_buffers_.erase(find_map_for_size_itr);
    }
  }
}

}  // namespace escher
