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

const impl::FramebufferPtr& FramebufferAllocator::ObtainFramebuffer(
    const RenderPassInfo& info, bool allow_render_pass_creation) {
  TRACE_DURATION("gfx", "escher::impl::FramebufferAllocator::ObtainFramebuffer");

  // We need the render-pass to generate the hash used to look up a framebuffer.  If the
  // render-pass doesn't exist, we assume that no framebuffer does either.  This is currently
  // always true (since |RenderPassCache| never deletes cache entries), and is a safe assumption
  // going forward (we should always evict items from this cache at least as frequently as from the
  // render-pass cache).
  auto& render_pass = render_pass_cache_->ObtainRenderPass(info, allow_render_pass_creation);
  FX_DCHECK(render_pass || !allow_render_pass_creation);
  if (!render_pass) {
    // We're returning "const Ptr&" not "Ptr", so we must return a reference to a value that won't
    // immediately go out of scope.
    FX_LOGS(WARNING) << "FramebufferAllocator::ObtainFramebuffer(): no render-pass was found";
    const static impl::FramebufferPtr null_ptr;
    return null_ptr;
  }
  FX_DCHECK(render_pass);

  Hasher h;
  h.u64(render_pass->uid());

  for (uint32_t i = 0; i < info.num_color_attachments; i++) {
    FX_DCHECK(info.color_attachments[i]);
    h.u64(info.color_attachments[i]->uid());
  }

  if (info.depth_stencil_attachment) {
    h.u64(info.depth_stencil_attachment->uid());
  }

  // TODO(fxbug.dev/7167): track cache hit/miss rates.
  Hash hash = h.value();
  auto pair = framebuffer_cache_.Obtain(hash);
  if (!pair.second) {
    // The cache didn't already have a Framebuffer so it returns an empty
    // FramebufferPtr that we will point at a newly-created Framebuffer.
    //
    // TODO(fxbug.dev/7169): it smells weird to use an ObjectPool to hold possibly-null
    // RefPtrs and then fill them in here.  Maybe ObjectPool can be rejiggered
    // to make this more elegant?
    TRACE_DURATION("gfx", "escher::FramebufferAllocator::ObtainFramebuffer (creation)");
    FX_DCHECK(!pair.first->framebuffer);
    pair.first->framebuffer = fxl::MakeRefCounted<impl::Framebuffer>(recycler_, render_pass, info);
  }
  FX_DCHECK(pair.first->framebuffer);
  return pair.first->framebuffer;
}

}  // namespace impl
}  // namespace escher
