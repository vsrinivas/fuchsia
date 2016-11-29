// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/escher.h"

#include "examples/waterfall/scenes/scene.h"

class RingTricks1 : public Scene {
 public:
  RingTricks1(escher::VulkanContext* vulkan_context, escher::Escher* escher);
  ~RingTricks1();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count,
                        escher::Stage* stage) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr blue_;
  escher::MaterialPtr red_;
  escher::MaterialPtr pink_;
  escher::MaterialPtr green_;
  escher::MaterialPtr blue_green_;
  escher::MaterialPtr purple_;
  escher::MaterialPtr bg_;

  escher::MaterialPtr mc1_;
  escher::MaterialPtr mc2_;

  escher::MeshPtr ring_mesh1;

  FTL_DISALLOW_COPY_AND_ASSIGN(RingTricks1);
};
