// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_DEMO_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_DEMO_SCENE_H_

#include "garnet/examples/escher/waterfall/scenes/scene.h"
#include "lib/escher/escher.h"

class DemoScene : public Scene {
 public:
  DemoScene(Demo* demo);
  ~DemoScene();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage,
                        escher::PaperRenderQueue* render_queue) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr purple_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DemoScene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_DEMO_SCENE_H_
