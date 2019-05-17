// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_SAMPLER_H_
#define SRC_UI_LIB_ESCHER_VK_SAMPLER_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"

namespace escher {

// Wraps an vk::Sampler object, and exposes the extension data used to construct
// it, so that the same extension data can be used in other contexts (e.g., when
// creating vk::ImageView objects).
class Sampler : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Sampler(ResourceRecycler* resource_recycler, vk::Format format,
          vk::Filter filter, bool use_unnormalized_coordinates = false);
  ~Sampler() override;

  const vk::Sampler& vk() const { return sampler_; }

  // If this sampler is immutable, it can only be used with a descriptor
  // set/pipeline that has been pre-configured with this sampler.
  bool is_immutable() const { return is_immutable_; }

  // If this sampler has extension data, then any ImageViews that use this
  // sampler must be initialized with the same extension data.
  void* GetExtensionData() {
    return is_immutable_ ? &ycbcr_conversion_ : nullptr;
  }

 private:
  vk::Sampler sampler_;
  vk::SamplerYcbcrConversionInfo ycbcr_conversion_;
  bool is_immutable_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Sampler);
};

typedef fxl::RefPtr<Sampler> SamplerPtr;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_SAMPLER_H_
