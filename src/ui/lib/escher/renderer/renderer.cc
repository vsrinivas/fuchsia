// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/renderer.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/profiling/timestamp_profiler.h"
#include "src/ui/lib/escher/scene/stage.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/framebuffer.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

Renderer::Renderer(EscherWeakPtr weak_escher)
    : context_(weak_escher->vulkan_context()), escher_(std::move(weak_escher)) {
  escher()->IncrementRendererCount();
}

Renderer::~Renderer() { escher()->DecrementRendererCount(); }

}  // namespace escher
