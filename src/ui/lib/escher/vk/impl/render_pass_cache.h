// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/util/hash_map.h"

namespace escher {
namespace impl {

// A dead-simple cache for impl::RenderPasses.  No support yet for clearing
// passes that haven't been used for a long time.
class RenderPassCache {
 public:
  RenderPassCache(ResourceRecycler* recycler);
  ~RenderPassCache();

  const impl::RenderPassPtr& ObtainRenderPass(const RenderPassInfo& info);

  size_t size() const { return render_passes_.size(); }

 private:
  ResourceRecycler* const recycler_;
  HashMap<Hash, impl::RenderPassPtr> render_passes_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_RENDER_PASS_CACHE_H_
