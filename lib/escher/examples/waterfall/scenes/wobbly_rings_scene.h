// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/escher.h"

#include "examples/waterfall/scenes/scene.h"

class WobblyRingsScene : public Scene {
 public:
  WobblyRingsScene(escher::VulkanContext* vulkan_context,
                   escher::Escher* escher);
  ~WobblyRingsScene();

  void Init() override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count,
                        escher::Stage* stage) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr wobbly_rect_mesh_;

  escher::MaterialPtr blue_;
  escher::MaterialPtr pink_;
  escher::MaterialPtr green_;
  escher::MaterialPtr blue_green_;
  escher::MaterialPtr purple_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WobblyRingsScene);
};
