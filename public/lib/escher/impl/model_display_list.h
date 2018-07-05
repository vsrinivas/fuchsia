// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_H_
#define LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/impl/model_data.h"
#include "lib/escher/resources/resource.h"

namespace escher {
namespace impl {

class ModelDisplayList : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  struct Item {
    vk::DescriptorSet descriptor_set;
    ModelPipeline* pipeline;
    MeshPtr mesh;
    uint32_t stencil_reference;
  };

  ModelDisplayList(ResourceRecycler* resource_recycler,
                   vk::DescriptorSet stage_data, std::vector<Item> items,
                   std::vector<TexturePtr> textures,
                   std::vector<ResourcePtr> resources);

  const std::vector<Item>& items() { return items_; }
  const std::vector<TexturePtr>& textures() { return textures_; }

  // TODO: consider rename
  vk::DescriptorSet stage_data() const { return stage_data_; }

 private:
  vk::DescriptorSet stage_data_;

  std::vector<Item> items_;
  std::vector<TexturePtr> textures_;
  std::vector<ResourcePtr> resources_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModelDisplayList);
};

typedef fxl::RefPtr<ModelDisplayList> ModelDisplayListPtr;

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_H_
