// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/memory/ref_counted.h"

class Pipeline;
using PipelinePtr = ftl::RefPtr<Pipeline>;

// Encapsulates a Vulkan pipeline that was compiled from GLSL source code.
// All Shadertoy pipelines share the same pipeline layout.
class Pipeline : public ftl::RefCountedThreadSafe<Pipeline> {
 public:
  Pipeline();
};
