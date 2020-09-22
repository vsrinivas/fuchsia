// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_

#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/paper/paper_render_funcs.h"
#include "src/ui/lib/escher/renderer/render_queue_context.h"

namespace escher {

// Provided to |PaperDrawCalls| via |PaperRenderQueueContext|.
enum class PaperRendererDrawMode : uint8_t {
  kAmbient = 0,
  kDepthOnly = 1,
  kShadowVolumeGeometry = 2,
  kShadowVolumeLighting = 3,
  kTranslucent = 4,
  kEnumCount
};

// Extend |RenderQueueContext| with additional fields that are used by
// the |PaperDrawCalls| enqueued in a |PaperRenderQueue|.
class PaperRenderQueueContext final : public RenderQueueContext {
 public:
  PaperRenderQueueContext();
  ~PaperRenderQueueContext();

  PaperRendererDrawMode draw_mode() const { return draw_mode_; }
  void set_draw_mode(PaperRendererDrawMode draw_mode) { draw_mode_ = draw_mode; }

  // TODO(fxbug.dev/7249): Providing the shader-program to the render-func via the
  // context works fine for now, but we will need a new approach.
  ShaderProgram* shader_program() const { return shader_program_.get(); }
  void set_shader_program(ShaderProgramPtr program);

 private:
  PaperRendererDrawMode draw_mode_ = PaperRendererDrawMode::kAmbient;
  ShaderProgramPtr shader_program_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
