// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/util/hash_cache.h"
#include "src/ui/lib/escher/vk/impl/framebuffer.h"

namespace escher {
namespace impl {

// FramebufferAllocator wraps HashCache to provide a frame-based cache for
// Vulkan framebuffers.
class FramebufferAllocator {
 public:
  FramebufferAllocator(ResourceRecycler* recycler, impl::RenderPassCache* render_pass_cache);
  const impl::FramebufferPtr& ObtainFramebuffer(const RenderPassInfo& info);

  void BeginFrame() { framebuffer_cache_.BeginFrame(); }
  void Clear() { framebuffer_cache_.Clear(); }

 private:
  struct CacheItem : public HashCacheItem<CacheItem> {
    impl::FramebufferPtr framebuffer;
  };

  ResourceRecycler* const recycler_;
  impl::RenderPassCache* const render_pass_cache_;

  // If this template is changed to have a non-default FramesUntilEviction
  // value, be sure to change all other HashCaches used by the Frame class
  // (e.g., DescriptorSetAllocator).
  HashCache<CacheItem, DefaultObjectPoolPolicy<CacheItem>> framebuffer_cache_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_
