// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/pipeline_layout_cache.h"

#include "src/ui/lib/escher/util/hasher.h"

namespace escher {
namespace impl {

PipelineLayoutCache::PipelineLayoutCache(ResourceRecycler* recycler)
    : recycler_(recycler) {}

PipelineLayoutCache::~PipelineLayoutCache() = default;

const PipelineLayoutPtr& PipelineLayoutCache::ObtainPipelineLayout(
    const PipelineLayoutSpec& spec) {
  auto it = layouts_.find(spec.hash());
  if (it != end(layouts_)) {
    FXL_DCHECK(it->second->spec() == spec);
    return it->second;
  }

  auto pair = layouts_.insert(std::make_pair(
      spec.hash(), fxl::MakeRefCounted<PipelineLayout>(recycler_, spec)));
  return pair.first->second;
}

void PipelineLayoutCache::Clear() { layouts_.clear(); }

}  // namespace impl
}  // namespace escher
