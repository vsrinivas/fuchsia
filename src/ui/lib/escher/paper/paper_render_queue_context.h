// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_

#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/paper/paper_render_funcs.h"
#include "src/ui/lib/escher/renderer/render_queue_context.h"

namespace escher {

// Extend |RenderQueueContext| with additional fields that are used by
// the |PaperDrawCalls| enqueued in a |PaperRenderQueue|.
class PaperRenderQueueContext final : public RenderQueueContext {
 public:
  PaperRenderQueueContext();
  ~PaperRenderQueueContext();

  // TODO(fxbug.dev/7249): Providing the shader-program to the render-func via the
  // context works fine for now, but we will need a new approach.
  ShaderProgram* shader_program() const { return shader_program_.get(); }
  void set_shader_program(ShaderProgramPtr program);

 private:
  ShaderProgramPtr shader_program_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
