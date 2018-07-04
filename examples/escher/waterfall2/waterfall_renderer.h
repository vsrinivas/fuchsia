// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/renderer/renderer.h"

class WaterfallRenderer;
using WaterfallRendererPtr = fxl::RefPtr<WaterfallRenderer>;

class WaterfallRenderer final : public escher::Renderer {
 public:
  static WaterfallRendererPtr New(escher::EscherWeakPtr escher,
                                  escher::ShaderProgramPtr program);

  void DrawFrame(const escher::FramePtr& frame, const escher::Stage& stage,
                 const escher::Model& model, const escher::Camera& camera,
                 const escher::ImagePtr& color_image_out);

  ~WaterfallRenderer() override;

  // Set the number of depth images that the renderer should round-robin
  // through.
  void SetNumDepthBuffers(size_t count);

 private:
  explicit WaterfallRenderer(escher::EscherWeakPtr,
                             escher::ShaderProgramPtr program);

  escher::ShaderProgramPtr program_;
  escher::BufferPtr uniforms_;
  std::vector<escher::TexturePtr> depth_buffers_;
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_RENDERER_H_
