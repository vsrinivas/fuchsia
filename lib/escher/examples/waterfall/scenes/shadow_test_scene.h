// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/scene/model.h"

class ShadowTestScene {
 public:
  ShadowTestScene();
  ~ShadowTestScene();

  escher::Model GetModel(const escher::SizeI& size);

 private:
  escher::Material card_material_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(ShadowTestScene);
};
