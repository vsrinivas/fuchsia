// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_

#include "src/ui/examples/escher/rainfall/scenes/scene.h"

// Flatland Demo Scene which gives the illusion of endlessly
// falling rectangles.
class FlatlandDemoScene2 : public Scene {
 public:
  explicit FlatlandDemoScene2(RainfallDemo* demo);
  ~FlatlandDemoScene2();

  // |Scene|
  void Init() override;

  // |Scene|
  void Update(const escher::Stopwatch& stopwatch) override;

  std::vector<escher::RectangleRenderable>& renderables() override { return renderables_; }

 private:
  std::vector<escher::RectangleRenderable> renderables_;
  std::vector<uint32_t> fall_speed_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_
