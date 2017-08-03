// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/escher.h"

#include "examples/waterfall/scenes/scene.h"

class UberScene2 : public Scene {
 public:
  UberScene2(Demo* demo);
  ~UberScene2();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count,
                        escher::Stage* stage) override;

  escher::Model* UpdateOverlay(const escher::Stopwatch& stopwatch,
                               uint64_t frame_count,
                               uint32_t width,
                               uint32_t height) override;

 private:
  std::unique_ptr<escher::Model> model_;
  std::unique_ptr<escher::Model> overlay_model_;

  escher::MaterialPtr blue_;
  escher::MaterialPtr red_;
  escher::MaterialPtr purple_;
  escher::MaterialPtr bg_;
  escher::MaterialPtr gray1_;
  escher::MaterialPtr gray2_;

  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr ring_mesh4_;
  escher::MeshPtr ring_mesh5_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UberScene2);
};
