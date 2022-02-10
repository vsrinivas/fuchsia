// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_

#include "src/ui/examples/escher/rainfall/scenes/scene.h"

// Flatland Demo Scene which gives the illusion of endlessly
// falling rectangles.
class FlatlandDemoScene2 : public RainfallScene {
 public:
  explicit FlatlandDemoScene2(RainfallDemo* demo);
  ~FlatlandDemoScene2() override;

  // |RainfallScene|
  void Init() override;

  // |RainfallScene|
  void Update(const escher::Stopwatch& stopwatch) override;

  std::vector<escher::Rectangle2D>& renderables() override { return renderables_; }

  std::vector<escher::RectangleCompositor::ColorData>& color_data() override { return color_data_; }

 private:
  std::vector<escher::Rectangle2D> renderables_;
  std::vector<escher::RectangleCompositor::ColorData> color_data_;
  std::vector<uint32_t> fall_speed_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_FLATLAND_DEMO_SCENE2_H_
