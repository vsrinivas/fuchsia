// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/renderer/display_renderer.h"

#include "apps/mozart/src/scene/resources/camera.h"
#include "apps/mozart/src/scene/resources/dump_visitor.h"
#include "escher/escher.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace scene {

DisplayRenderer::DisplayRenderer(Session* session,
                                 ResourceId id,
                                 FrameScheduler* frame_scheduler,
                                 escher::Escher* escher,
                                 escher::VulkanSwapchain swapchain)
    : Renderer(session, id, frame_scheduler),
      paper_renderer_(escher->NewPaperRenderer()),
      swapchain_helper_(std::move(swapchain), paper_renderer_) {}

DisplayRenderer::~DisplayRenderer() {}

void DisplayRenderer::DrawFrame() {
  float width = static_cast<float>(swapchain_helper_.swapchain().width);
  float height = static_cast<float>(swapchain_helper_.swapchain().height);

  if (FTL_VLOG_IS_ON(3)) {
    std::ostringstream output;
    DumpVisitor visitor(output);
    Accept(&visitor);
    FTL_VLOG(3) << "Renderer dump\n" << output.str();
  }

  FTL_DCHECK(camera());
  FTL_DCHECK(camera()->scene());
  escher::Model model(
      CreateDisplayList(camera()->scene(), escher::vec2(width, height)));

  escher::Stage stage;
  stage.Resize(escher::SizeI(width, height), 1.0, escher::SizeI(0, 0));
  constexpr float kTop = 50;
  constexpr float kBottom = 0;
  stage.set_viewing_volume({width, height, kTop, kBottom});
  stage.set_key_light(escher::DirectionalLight(
      escher::vec2(1.5f * M_PI, 1.5f * M_PI), 0.15f * M_PI, 0.7f));
  stage.set_fill_light(escher::AmbientLight(0.3f));

  swapchain_helper_.DrawFrame(stage, model);
}

}  // namespace scene
}  // namespace mozart
