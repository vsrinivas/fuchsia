// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"

#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace escher {
namespace impl {

RenderPassCache::RenderPassCache(ResourceRecycler* recycler) : recycler_(recycler) {}

RenderPassCache::~RenderPassCache() = default;

const impl::RenderPassPtr& RenderPassCache::ObtainRenderPass(const RenderPassInfo& rpi,
                                                             bool allow_render_pass_creation) {
  TRACE_DURATION("gfx", "escher::impl::RenderPassCache::ObtainRenderPass");
  Hasher h;

  // TODO(fxbug.dev/7166): take advantage of lazily-allocated memory for transient
  // subpass attachments in tile-based GPUs.  This involves setting a bit for
  // each transient attachment, both color and depth-stencil.
  uint32_t lazy = 0;

  for (size_t i = 0; i < rpi.num_color_attachments; i++) {
    auto& attachment_info = rpi.color_attachment_infos[i];
    h.u32(EnumCast(attachment_info.format));
    h.u32(EnumCast(attachment_info.swapchain_layout));
    h.u32(attachment_info.sample_count);
    if (attachment_info.is_transient) {
      lazy |= 1u << i;
    }
  }

  if (rpi.depth_stencil_attachment_info.format != vk::Format::eUndefined) {
    h.u32(EnumCast(rpi.depth_stencil_attachment_info.format));
    h.u32(EnumCast(rpi.depth_stencil_attachment_info.swapchain_layout));
    // TODO(fxbug.dev/7166): See above.  We don't check whether the depth-stencil attachment is
    // transient, but it seems like we probably should.
  }

  uint32_t num_subpasses = rpi.subpasses.size();
  h.u32(num_subpasses);
  for (size_t i = 0; i < num_subpasses; i++) {
    h.u32(rpi.subpasses[i].num_color_attachments);
    h.u32(rpi.subpasses[i].num_input_attachments);
    h.u32(rpi.subpasses[i].num_resolve_attachments);
    h.u32(EnumCast(rpi.subpasses[i].depth_stencil_mode));
    for (unsigned j = 0; j < rpi.subpasses[i].num_color_attachments; j++)
      h.u32(rpi.subpasses[i].color_attachments[j]);
    for (unsigned j = 0; j < rpi.subpasses[i].num_input_attachments; j++)
      h.u32(rpi.subpasses[i].input_attachments[j]);
    for (unsigned j = 0; j < rpi.subpasses[i].num_resolve_attachments; j++)
      h.u32(rpi.subpasses[i].resolve_attachments[j]);
  }

  h.u32(rpi.num_color_attachments);
  h.u32(rpi.op_flags);
  h.u32(rpi.clear_attachments);
  h.u32(rpi.load_attachments);
  h.u32(rpi.store_attachments);
  h.u32(lazy);

  // TODO(fxbug.dev/7167): track cache hit/miss rates.
  // TODO(fxbug.dev/7166): pass |lazy| to RenderPass constructor; compare against
  // retrieved RenderPass to make sure that they match.
  Hash hash = h.value();
  auto it = render_passes_.find(hash);
  if (it != end(render_passes_)) {
    return it->second;
  }

  if (!allow_render_pass_creation) {
    // If the application called set_unexpected_lazy_creation_callback(), give it a chance to allow
    // lazy creation instead of returning nullptr.  If the closure returns true, lazy creation is
    // allowed, thus overriding |allow_render_pass_creation|.
    if (!unexpected_lazy_creation_callback_ || !unexpected_lazy_creation_callback_(rpi)) {
      // We're returning "const Ptr&" not "Ptr", so we must return a reference to a value that won't
      // immediately go out of scope.
      FX_LOGS(WARNING) << "lazy render-pass creation is not allowed for: " << rpi;
      const static impl::RenderPassPtr null_ptr;
      return null_ptr;
    }
  }

  TRACE_DURATION("gfx", "escher::RenderPassCache::ObtainRenderPass (creation)");

  auto pair = render_passes_.insert(
      std::make_pair(hash, fxl::MakeRefCounted<impl::RenderPass>(recycler_, rpi)));
  FX_DCHECK(pair.second);
  return pair.first->second;
}

}  // namespace impl
}  // namespace escher
