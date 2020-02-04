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

  // Obtain a cached Framebuffer, or lazily create a new one if necessary.  Creating a VkFramebuffer
  // requires a VkRenderPass; if necessary the render pass will also be created lazily, but only if
  // |allow_render_pass_creation| is true (otherwise a CHECK will fail).  This allows the
  // application to require all render passes to be created at specific times (e.g. startup, or
  // loading a particular game level) to avoid jank due to lazy creation of render passes (and
  // pipelines).
  //
  // NOTE: pipelines cannot be created without a render pass, and render passes are useless without
  // a pipeline.  Therefore, we could allow lazy render pass creation and rely on the inevitable
  // failed CHECK during pipeline creation.  However, it is better to also disallow render pass
  // creation because then the source of the problem is obvious, not lost among the many other
  // reasons that might trigger lazy pipeline creation.
  const impl::FramebufferPtr& ObtainFramebuffer(const RenderPassInfo& info,
                                                bool allow_render_pass_creation);

  void BeginFrame() { framebuffer_cache_.BeginFrame(); }
  void Clear() { framebuffer_cache_.Clear(); }

  size_t size() const { return framebuffer_cache_.size(); }

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
