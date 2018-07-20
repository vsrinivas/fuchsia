// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS2_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS2_H_

#include "lib/escher/escher.h"
#include "lib/escher/shape/rounded_rect_factory.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"

class RingTricks2 : public Scene {
 public:
  RingTricks2(Demo* demo);
  ~RingTricks2();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage,
                        escher::PaperRenderQueue* render_queue) override;

 private:
  escher::RoundedRectFactory factory_;

  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr red_;
  escher::MaterialPtr bg_;

  escher::MaterialPtr color1_;
  escher::MaterialPtr color2_;

  escher::MaterialPtr gradient_;

  escher::MeshPtr ring_mesh1_;

  escher::MeshPtr rounded_rect1_;
  escher::MeshPtr rounded_rect2_;
  escher::MeshPtr rounded_rect3_;
  escher::MeshPtr sphere_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RingTricks2);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS2_H_
