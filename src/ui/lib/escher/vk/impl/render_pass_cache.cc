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

const impl::RenderPassPtr& RenderPassCache::ObtainRenderPass(const RenderPassInfo& info) {
  TRACE_DURATION("gfx", "escher::impl::RenderPassCache::ObtainRenderPass");

  Hasher h;

  // TODO(ES-73): take advantage of lazily-allocated memory for transient
  // subpass attachments in tile-based GPUs.  This involves setting a bit for
  // each transient attachment, both color and depth-stencil.
  uint32_t lazy = 0;

  for (size_t i = 0; i < info.num_color_attachments; i++) {
    auto& image_view = info.color_attachments[i];
    FXL_DCHECK(image_view);
    h.u32(EnumCast(image_view->image()->format()));
    h.u32(EnumCast(image_view->image()->swapchain_layout()));
    h.u32(image_view->image()->info().sample_count);
    if (image_view->image()->is_transient()) {
      lazy |= 1u << i;
    }
  }

  if (info.depth_stencil_attachment) {
    h.u32(EnumCast(info.depth_stencil_attachment->image()->format()));
    h.u32(EnumCast(info.depth_stencil_attachment->image()->swapchain_layout()));
  }

  uint32_t num_subpasses = info.subpasses.size();
  h.u32(num_subpasses);
  for (size_t i = 0; i < num_subpasses; i++) {
    h.u32(info.subpasses[i].num_color_attachments);
    h.u32(info.subpasses[i].num_input_attachments);
    h.u32(info.subpasses[i].num_resolve_attachments);
    h.u32(EnumCast(info.subpasses[i].depth_stencil_mode));
    for (unsigned j = 0; j < info.subpasses[i].num_color_attachments; j++)
      h.u32(info.subpasses[i].color_attachments[j]);
    for (unsigned j = 0; j < info.subpasses[i].num_input_attachments; j++)
      h.u32(info.subpasses[i].input_attachments[j]);
    for (unsigned j = 0; j < info.subpasses[i].num_resolve_attachments; j++)
      h.u32(info.subpasses[i].resolve_attachments[j]);
  }

  h.u32(info.num_color_attachments);
  h.u32(info.op_flags);
  h.u32(info.clear_attachments);
  h.u32(info.load_attachments);
  h.u32(info.store_attachments);
  h.u32(lazy);

  // TODO(ES-74): track cache hit/miss rates.
  // TODO(ES-73): pass |lazy| to RenderPass constructor; compare against
  // retrieved RenderPass to make sure that they match.
  Hash hash = h.value();
  auto it = render_passes_.find(hash);
  if (it != end(render_passes_)) {
    return it->second;
  }

  TRACE_DURATION("gfx", "escher::RenderPassCache::ObtainRenderPass (creation)");
  auto pair = render_passes_.insert(
      std::make_pair(hash, fxl::MakeRefCounted<impl::RenderPass>(recycler_, info)));
  FXL_DCHECK(pair.second);
  return pair.first->second;
}

}  // namespace impl
}  // namespace escher
