// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/renderer.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

Renderer::Renderer(EscherWeakPtr weak_escher)
    : context_(weak_escher->vulkan_context()), escher_(std::move(weak_escher)) {
  escher()->IncrementRendererCount();
}

Renderer::~Renderer() { escher()->DecrementRendererCount(); }

}  // namespace escher
