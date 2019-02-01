// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_display_list.h"

#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/texture.h"

namespace escher {
namespace impl {

const ResourceTypeInfo ModelDisplayList::kTypeInfo(
    "ModelDisplayList", ResourceType::kResource,
    ResourceType::kImplModelDisplayList);

ModelDisplayList::ModelDisplayList(ResourceRecycler* resource_recycler,
                                   vk::DescriptorSet stage_data,
                                   std::vector<Item> items,
                                   std::vector<TexturePtr> textures,
                                   std::vector<ResourcePtr> resources)
    : Resource(resource_recycler),
      stage_data_(stage_data),
      items_(std::move(items)),
      textures_(std::move(textures)),
      resources_(std::move(resources)) {}

}  // namespace impl
}  // namespace escher
