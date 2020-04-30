// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/pipeline_layout_cache.h"

#include "src/ui/lib/escher/util/hasher.h"

namespace escher {
namespace impl {

PipelineLayoutCache::PipelineLayoutCache(ResourceRecycler* recycler) : recycler_(recycler) {}

PipelineLayoutCache::~PipelineLayoutCache() = default;

const PipelineLayoutPtr& PipelineLayoutCache::ObtainPipelineLayout(const PipelineLayoutSpec& spec) {
  auto pair = layouts_.Obtain(spec.hash());

  if (!pair.second) {
    FX_DCHECK(!pair.first->layout);
    pair.first->layout = fxl::MakeRefCounted<PipelineLayout>(recycler_, spec);
  }
  FX_DCHECK(pair.first->layout);
  return pair.first->layout;
}

void PipelineLayoutCache::BeginFrame() { layouts_.BeginFrame(); }

}  // namespace impl
}  // namespace escher
