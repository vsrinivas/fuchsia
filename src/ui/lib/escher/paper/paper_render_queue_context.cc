// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_render_queue_context.h"

#include "src/ui/lib/escher/vk/shader_program.h"

namespace escher {

PaperRenderQueueContext::PaperRenderQueueContext() {
  render_queue_func_to_use = 0U;
  reserved = 0U;
  client_data = 0U;
}

PaperRenderQueueContext::~PaperRenderQueueContext() = default;

void PaperRenderQueueContext::set_shader_program(ShaderProgramPtr program) {
  shader_program_ = std::move(program);
}

}  // namespace escher
