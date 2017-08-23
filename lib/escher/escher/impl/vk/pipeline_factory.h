// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <future>

#include "escher/impl/vk/pipeline.h"
#include "lib/ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Interface which can be used to create a new Pipeline, given a PipelineSpec.
// The "type" field of the PipelineSpec allows the PipelineFactory to quickly
// decide whether it is able to create the requested pipeline, and also to
// validate whether the spec's "data" is compatible with the requested "type".
class PipelineFactory : public ftl::RefCountedThreadSafe<PipelineFactory> {
 public:
  virtual ~PipelineFactory() {}
  virtual std::future<PipelinePtr> NewPipeline(PipelineSpec spec) = 0;
};

typedef ftl::RefPtr<PipelineFactory> PipelineFactoryPtr;

}  // namespace impl
}  // namespace escher
