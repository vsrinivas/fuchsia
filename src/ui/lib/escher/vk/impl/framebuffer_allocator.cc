// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/framebuffer_allocator.h"

#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/impl/framebuffer.h"
#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"

namespace escher {
namespace impl {

FramebufferAllocator::FramebufferAllocator(ResourceRecycler* recycler,
                                           impl::RenderPassCache* render_pass_cache)
    : recycler_(recycler), render_pass_cache_(render_pass_cache) {}

const impl::FramebufferPtr& FramebufferAllocator::ObtainFramebuffer(const RenderPassInfo& info) {
  TRACE_DURATION("gfx", "escher::impl::FramebufferAllocator::ObtainFramebuffer");

  auto& render_pass = render_pass_cache_->ObtainRenderPass(info);
  FXL_DCHECK(render_pass);

  Hasher h;
  h.u64(render_pass->uid());

  for (uint32_t i = 0; i < info.num_color_attachments; i++) {
    FXL_DCHECK(info.color_attachments[i]);
    h.u64(info.color_attachments[i]->uid());
  }

  if (info.depth_stencil_attachment) {
    h.u64(info.depth_stencil_attachment->uid());
  }

  // TODO(ES-74): track cache hit/miss rates.
  Hash hash = h.value();
  auto pair = framebuffer_cache_.Obtain(hash);
  if (!pair.second) {
    // The cache didn't already have a Framebuffer so it returns an empty
    // FramebufferPtr that we will point at a newly-created Framebuffer.
    //
    // TODO(ES-76): it smells weird to use an ObjectPool to hold possibly-null
    // RefPtrs and then fill them in here.  Maybe ObjectPool can be rejiggered
    // to make this more elegant?
    TRACE_DURATION("gfx", "escher::FramebufferAllocator::ObtainFramebuffer (creation)");
    FXL_DCHECK(!pair.first->framebuffer);
    pair.first->framebuffer = fxl::MakeRefCounted<impl::Framebuffer>(recycler_, render_pass, info);
  }
  FXL_DCHECK(pair.first->framebuffer);
  return pair.first->framebuffer;
}

}  // namespace impl
}  // namespace escher
