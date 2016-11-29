// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/escher.h"

#include "examples/waterfall/scenes/scene.h"

class UberScene2 : public Scene {
 public:
  UberScene2(escher::VulkanContext* vulkan_context, escher::Escher* escher);
  ~UberScene2();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count,
                        escher::Stage* stage) override;

 private:
  std::unique_ptr<escher::Model> model_;

  escher::MaterialPtr blue_;
  escher::MaterialPtr red_;
  escher::MaterialPtr purple_;
  escher::MaterialPtr bg_;

  escher::MeshPtr ring_mesh1_;
  escher::MeshPtr ring_mesh2_;
  escher::MeshPtr ring_mesh3_;
  escher::MeshPtr ring_mesh4_;
  escher::MeshPtr ring_mesh5_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UberScene2);
};
