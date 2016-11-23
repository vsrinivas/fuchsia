// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/scene.h"

Scene::Scene(escher::VulkanContext* vulkan_context, escher::Escher* escher)
    : vulkan_context_(vulkan_context), escher_(escher) {}

Scene::~Scene() {}
