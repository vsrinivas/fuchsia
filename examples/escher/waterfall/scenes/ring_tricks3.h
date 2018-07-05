// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS3_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS3_H_

#include "lib/escher/escher.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"

class RingTricks3 : public Scene {
 public:
  RingTricks3(Demo* demo);
  ~RingTricks3();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr bg_;

  escher::MaterialPtr color1_;
  escher::MaterialPtr color2_;

  escher::MeshPtr ring_mesh1_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RingTricks3);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_RING_TRICKS3_H_
