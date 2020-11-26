// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_

#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/paper/paper_render_funcs.h"
#include "src/ui/lib/escher/paper/paper_shader_list.h"
#include "src/ui/lib/escher/renderer/render_queue_context.h"

namespace escher {

// Extend |RenderQueueContext| with additional fields that are used by
// the |PaperDrawCalls| enqueued in a |PaperRenderQueue|.
class PaperRenderQueueContext final : public RenderQueueContext {
 public:
  PaperRenderQueueContext();
  ~PaperRenderQueueContext();

  PaperShaderListSelector shader_selector() const { return shader_selector_; }
  void set_shader_selector(PaperShaderListSelector sel) { shader_selector_ = sel; }

 private:
  PaperShaderListSelector shader_selector_ = PaperShaderListSelector::kAmbientLighting;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_CONTEXT_H_
