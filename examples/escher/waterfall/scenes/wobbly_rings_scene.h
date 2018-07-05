// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_RINGS_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_RINGS_SCENE_H_

#include "lib/escher/escher.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"
#include "lib/escher/geometry/types.h"

using escher::vec3;

class WobblyRingsScene : public Scene {
 public:
  WobblyRingsScene(Demo* demo, vec3 clear_color, vec3 ring1_color,
                   vec3 ring2_color, vec3 ring3_color, vec3 circle_color,
                   vec3 checkerboard_color);
  ~WobblyRingsScene();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage) override;

 private:
  std::unique_ptr<escher::Model> model_;

  vec3 clear_color_;
  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr wobbly_rect_mesh_;
  escher::MaterialPtr circle_color_;
  escher::MaterialPtr clip_color_;
  escher::MaterialPtr ring1_color_;
  escher::MaterialPtr ring2_color_;
  escher::MaterialPtr ring3_color_;
  escher::MaterialPtr checkerboard_material_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WobblyRingsScene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_WOBBLY_RINGS_SCENE_H_
