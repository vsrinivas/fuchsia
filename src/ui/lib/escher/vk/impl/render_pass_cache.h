// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_

#include <lib/fit/function.h>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace escher {
namespace impl {

// A dead-simple cache for impl::RenderPasses.  No support yet for clearing long-unused passes.
class RenderPassCache {
 public:
  RenderPassCache(ResourceRecycler* recycler);
  ~RenderPassCache();

  // Tries to find a cached render-pass that matches |info|.  If unsuccessful, then:
  //   - if |allow_render_pass_creation| == false, return nullptr.
  //     - see set_unexpected_lazy_creation_callback(), which modifies this behavior.
  //   - otherwise create, cache, and return a new render-pass.
  //
  // NOTE: creating a new render-pass will DCHECK if |info| is not valid.
  const impl::RenderPassPtr& ObtainRenderPass(const RenderPassInfo& info,
                                              bool allow_render_pass_creation);

  // When ObtainRenderPass() is called with |allow_render_pass_creation| == false, and no existing
  // render-pass is found, the default behavior is to return nullptr.  This allows clients to
  // override that behavior, allowing them to respond to unexpected render-pass creation at a higher
  // level, e.g. perhaps uploading to appear on an analytics dashboard.
  //
  // If the callback returns true, the render-pass will be created lazily anyway, even though
  // |allow_render_pass_creation| was false.  Otherwise, ObtainRenderPass() will return nullptr,
  // just as if set_unexpected_lazy_creation_callback() had not been previously called.
  using UnexpectedLazyCreationCallback = fit::function<bool(const RenderPassInfo&)>;
  void set_unexpected_lazy_creation_callback(UnexpectedLazyCreationCallback callback) {
    unexpected_lazy_creation_callback_ = std::move(callback);
  }

  size_t size() const { return render_passes_.size(); }

 private:
  ResourceRecycler* const recycler_;
  HashMap<Hash, impl::RenderPassPtr> render_passes_;
  UnexpectedLazyCreationCallback unexpected_lazy_creation_callback_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_
