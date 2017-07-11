// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_VK_VMO_IMAGE_GENERATOR_H_
#define APPS_MOZART_LIB_SKIA_VK_VMO_IMAGE_GENERATOR_H_

#include "apps/mozart/services/buffers/cpp/buffer_consumer.h"
#include "apps/mozart/services/buffers/cpp/buffer_fence.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "third_party/skia/include/gpu/vk/GrVkTypes.h"
#include "third_party/skia/include/private/GrTextureProxy.h"

namespace mozart {

// Takes a |mtl::SharedVmo| and uses Magma extensions to import as
// VkDeviceMemory. That VkDeviceMemory is then wrapped to create a Skia texture.
class VkVmoImageGenerator : public SkImageGenerator {
 public:
  VkVmoImageGenerator(const SkImageInfo& image_info,
                      ftl::RefPtr<mtl::SharedVmo> vmo);

  ~VkVmoImageGenerator() override;

  sk_sp<GrTextureProxy> onGenerateTexture(GrContext*,
                                          const SkImageInfo& info,
                                          const SkIPoint& origin,
                                          SkTransferFunctionBehavior behavior) override;

 private:
  ftl::RefPtr<mtl::SharedVmo> shared_vmo_ = nullptr;
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_VK_VMO_SKIA_IMAGE_GENERATOR_H_
