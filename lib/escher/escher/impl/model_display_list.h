// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/model_data.h"
#include "escher/impl/resource.h"

namespace escher {
namespace impl {

class ModelDisplayList : public Resource {
 public:
  struct Item {
    vk::DescriptorSet descriptor_sets[ModelData::PerObject::kDescriptorCount];
    ModelPipeline* pipeline;
    MeshPtr mesh;
    uint32_t stencil_reference;
  };

  ModelDisplayList(vk::DescriptorSet stage_data,
                   std::vector<Item> items,
                   std::vector<TexturePtr> textures,
                   std::vector<ResourcePtr> resources)
      : Resource(nullptr),
        stage_data_(stage_data),
        items_(std::move(items)),
        textures_(std::move(textures)),
        resources_(std::move(resources)) {}

  const std::vector<Item>& items() { return items_; }
  const std::vector<TexturePtr>& textures() { return textures_; }

  // TODO: consider rename
  vk::DescriptorSet stage_data() const { return stage_data_; }

 private:
  vk::DescriptorSet stage_data_;

  std::vector<Item> items_;
  std::vector<TexturePtr> textures_;
  std::vector<ResourcePtr> resources_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelDisplayList);
};

typedef ftl::RefPtr<ModelDisplayList> ModelDisplayListPtr;

}  // namespace impl
}  // namespace escher
