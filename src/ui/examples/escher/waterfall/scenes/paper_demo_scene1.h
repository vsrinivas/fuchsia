// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE1_H_
#define SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE1_H_

#include "src/ui/examples/escher/waterfall/scenes/scene.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/paper/paper_shape_cache.h"
#include "src/ui/lib/escher/shape/rounded_rect.h"

// Demo scene specifically designed to exercise the new PaperRenderer components
// (e.g. PaperShapeCache and PaperRenderQueue).
class PaperDemoScene1 : public Scene {
 public:
  explicit PaperDemoScene1(Demo* demo);
  ~PaperDemoScene1();

  // |Scene|
  void Init(escher::PaperScene* scene) override;

  // |Scene|
  void Update(const escher::Stopwatch& stopwatch, escher::PaperScene* scene,
              escher::PaperRenderer* renderer) override;

 private:
  struct AnimatedState {
    float cycle_duration;
    size_t cycle_count_before_pause;
    float inter_cycle_pause_duration;

    // Return an animation parameter between 0 and 1;
    float Update(float current_time_sec);

    // Private.
    float state_start_time = 0.f;  // seconds;
    bool paused = false;
  };

  struct RectState {
    AnimatedState animation;
    escher::MaterialPtr material;

    // Start and end animation positions.
    escher::vec3 pos1, pos2;

    // Start and end rounded-rect shape specs.
    escher::RoundedRectSpec spec1, spec2;
  };

  struct ClipPlaneState {
    AnimatedState animation;

    // Start and end position of a point on an oriented clip plane.
    escher::vec2 pos1, pos2;

    // Start and end direction of the normal for an oriented clip plane.
    float radians1, radians2;

    // Compute an animation parameter and return the corresponding clip plane.
    escher::plane3 Update(float current_time_sec);
  };

  std::vector<RectState> rectangles_;
  std::vector<ClipPlaneState> world_space_clip_planes_;
  std::vector<ClipPlaneState> object_space_clip_planes_;

  escher::MaterialPtr red_;
  escher::MaterialPtr bg_;
  escher::MaterialPtr color1_;
  escher::MaterialPtr color2_;

  RectState translucent_rectangle_;
  escher::MaterialPtr translucent_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PaperDemoScene1);
};

#endif  // SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_DEMO_SCENE1_H_
