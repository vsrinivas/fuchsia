// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_

#include "garnet/examples/escher/waterfall/scenes/scene.h"

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/paper/paper_render_queue.h"
#include "lib/escher/renderer/renderer.h"

class WaterfallRenderer;
using WaterfallRendererPtr = fxl::RefPtr<WaterfallRenderer>;

class WaterfallRenderer final : public escher::Renderer {
 public:
  static WaterfallRendererPtr New(escher::EscherWeakPtr escher);

  void DrawFrame(const escher::FramePtr& frame, escher::Stage* stage,
                 const escher::Camera& camera,
                 const escher::Stopwatch& stopwatch, uint64_t frame_count,
                 Scene* scene, const escher::ImagePtr& output_image);

  ~WaterfallRenderer() override;

  // Set the number of depth images that the renderer should round-robin
  // through.
  void SetNumDepthBuffers(size_t count);

 private:
  explicit WaterfallRenderer(escher::EscherWeakPtr escher);

  void BeginRenderPass(const escher::FramePtr& frame,
                       const escher::ImagePtr& output_image);
  void EndRenderPass(const escher::FramePtr& frame);

  escher::PaperRenderQueue render_queue_;
  std::vector<escher::TexturePtr> depth_buffers_;
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_
