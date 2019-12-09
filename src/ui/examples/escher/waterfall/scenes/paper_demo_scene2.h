// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE2_H_
#define SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE2_H_

#include "src/ui/examples/escher/waterfall/scenes/scene.h"
#include "src/ui/lib/escher/escher.h"

// Demo scene designed to test graphics debug components such as DebugRects.
class PaperDemoScene2 : public Scene {
 public:
  explicit PaperDemoScene2(Demo* demo);
  ~PaperDemoScene2();

  // |Scene|
  void Init(escher::PaperScene* scene) override;

  // |Scene|
  void Update(const escher::Stopwatch& stopwatch, escher::PaperScene* scene,
              escher::PaperRenderer* renderer) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(PaperDemoScene2);
};

#endif  // SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE2_H_
