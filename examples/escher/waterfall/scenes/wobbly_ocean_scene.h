// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_OCEAN_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_OCEAN_SCENE_H_

#include "lib/escher/escher.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"

class WobblyOceanScene : public Scene {
 public:
  WobblyOceanScene(Demo* demo);
  ~WobblyOceanScene();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage,
                        escher::PaperRenderQueue* render_queue) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr bg_;

  escher::MaterialPtr color1_;
  escher::MaterialPtr color2_;
  escher::MaterialPtr color3_;
  escher::MaterialPtr color4_;
  escher::MaterialPtr checkerboard_material_;

  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr wobbly_ocean_mesh_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WobblyOceanScene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_OCEAN_SCENE_H_
