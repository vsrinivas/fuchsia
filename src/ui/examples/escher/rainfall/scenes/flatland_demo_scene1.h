// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_

#include "src/ui/examples/escher/rainfall/scenes/scene.h"

// Flatland Demo Scene which shows a ring of rotating rectangles
// which collapse and expand.
class FlatlandDemoScene1 : public RainfallScene {
 public:
  explicit FlatlandDemoScene1(RainfallDemo* demo);
  ~FlatlandDemoScene1() override;

  // |RainfallScene|
  void Init() override;

  // |RainfallScene|
  void Update(const escher::Stopwatch& stopwatch) override;

  std::vector<escher::Rectangle2D>& renderables() override { return renderables_; }

  std::vector<escher::RectangleCompositor::ColorData>& color_data() override { return color_data_; }

 private:
  std::vector<escher::Rectangle2D> renderables_;
  std::vector<escher::RectangleCompositor::ColorData> color_data_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE1_H_
