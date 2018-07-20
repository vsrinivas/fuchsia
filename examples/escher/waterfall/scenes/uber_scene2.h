// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_UBER_SCENE2_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_UBER_SCENE2_H_

#include "lib/escher/escher.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"

class UberScene2 : public Scene {
 public:
  UberScene2(Demo* demo);
  ~UberScene2();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage,
                        escher::PaperRenderQueue* render_queue) override;

  escher::Model* UpdateOverlay(const escher::Stopwatch& stopwatch,
                               uint64_t frame_count, uint32_t width,
                               uint32_t height) override;

 private:
  std::unique_ptr<escher::Model> model_;
  std::unique_ptr<escher::Model> overlay_model_;

  escher::MaterialPtr blue_;
  escher::MaterialPtr red_;
  escher::MaterialPtr purple_;
  escher::MaterialPtr bg_;
  escher::MaterialPtr gray1_;
  escher::MaterialPtr gray2_;

  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr ring_mesh4_;
  escher::MeshPtr ring_mesh5_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UberScene2);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_UBER_SCENE2_H_
