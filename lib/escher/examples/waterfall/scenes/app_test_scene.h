// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/scene/model.h"

class AppTestScene {
 public:
  AppTestScene();
  ~AppTestScene();

  escher::Model GetModel(const escher::SizeI& size, const glm::vec2& focus);

 private:
  escher::Material app_bar_material_;
  escher::Material canvas_material_;
  escher::Material card_material_;
  escher::Material fab_material_;
  escher::Material green_material_;
  escher::Material null_material_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(AppTestScene);
};
