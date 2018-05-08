// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SHADOW_TEST_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SHADOW_TEST_SCENE_H_

#include "lib/escher/scene/model.h"
#include "lib/escher/scene/viewing_volume.h"
#include "lib/fxl/macros.h"

class ShadowTestScene {
 public:
  ShadowTestScene();
  ~ShadowTestScene();

  escher::Model GetModel(const escher::ViewingVolume& volume);

 private:
  escher::Material card_material_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShadowTestScene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SHADOW_TEST_SCENE_H_
