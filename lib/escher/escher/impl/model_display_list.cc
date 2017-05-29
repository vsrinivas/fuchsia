// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_display_list.h"

#include "escher/renderer/texture.h"
#include "escher/resources/resource_life_preserver.h"

namespace escher {
namespace impl {

const ResourceTypeInfo ModelDisplayList::kTypeInfo(
    "ModelDisplayList",
    ResourceType::kResource,
    ResourceType::kImplModelDisplayList);

ModelDisplayList::ModelDisplayList(ResourceLifePreserver* life_preserver,
                                   vk::DescriptorSet stage_data,
                                   std::vector<Item> items,
                                   std::vector<TexturePtr> textures,
                                   std::vector<ResourcePtr> resources)
    : Resource(life_preserver),
      stage_data_(stage_data),
      items_(std::move(items)),
      textures_(std::move(textures)),
      resources_(std::move(resources)) {}

}  // namespace impl
}  // namespace escher
