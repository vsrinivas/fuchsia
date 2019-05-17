// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_PIPELINE_LAYOUT_CACHE_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_PIPELINE_LAYOUT_CACHE_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/util/hash_map.h"

namespace escher {
namespace impl {

class PipelineLayoutCache {
 public:
  explicit PipelineLayoutCache(ResourceRecycler* recycler);
  ~PipelineLayoutCache();

  // Return a layout corresponding to the spec, creating a new one if none is
  // already present in the cache.
  const PipelineLayoutPtr& ObtainPipelineLayout(
      const PipelineLayoutSpec& layout);

  // TODO(ES-201): There is currently no eviction policy for this cache, as
  // Escher never calls this function.
  void Clear();

 private:
  ResourceRecycler* recycler_;
  HashMap<Hash, PipelineLayoutPtr> layouts_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_PIPELINE_LAYOUT_CACHE_H_
