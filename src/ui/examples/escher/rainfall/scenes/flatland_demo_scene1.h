// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_

#include "src/ui/examples/escher/rainfall/scenes/scene.h"

// Flatland Demo Scene which shows a ring of rotating rectangles
// which collapse and expand.
class FlatlandDemoScene1 : public Scene {
 public:
  explicit FlatlandDemoScene1(RainfallDemo* demo);
  ~FlatlandDemoScene1();

  // |Scene|
  void Init() override;

  // |Scene|
  void Update(const escher::Stopwatch& stopwatch) override;

  std::vector<escher::RectangleRenderable>& renderables() override { return renderables_; }

 private:
  std::vector<escher::RectangleRenderable> renderables_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_
