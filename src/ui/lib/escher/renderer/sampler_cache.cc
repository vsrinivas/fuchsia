// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/sampler_cache.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {

SamplerCache::SamplerCache(fxl::WeakPtr<ResourceRecycler> resource_recycler)
    : resource_recycler_(std::move(resource_recycler)) {}

SamplerPtr SamplerCache::ObtainSampler(vk::Filter filter, bool use_unnormalized_coordinates) {
  Key key{vk::Format::eUndefined, filter, use_unnormalized_coordinates};
  return ObtainSampler(key);
}

SamplerPtr SamplerCache::ObtainYuvSampler(vk::Format format, vk::Filter filter,
                                          bool use_unnormalized_coordinates) {
  FX_DCHECK(image_utils::IsYuvFormat(format));

  const vk::PhysicalDevice& physical_device = resource_recycler_->vulkan_context().physical_device;
  FX_DCHECK(physical_device);
  FX_DCHECK(impl::IsYuvConversionSupported(physical_device, format));

  Key key{format, filter, use_unnormalized_coordinates};
  return ObtainSampler(key);
}

SamplerPtr SamplerCache::ObtainSampler(const Key& key) {
  auto it = samplers_.find(key);
  if (it != samplers_.end()) {
    return it->second;
  }
  auto sampler = fxl::MakeRefCounted<Sampler>(resource_recycler_.get(), key.format, key.filter,
                                              key.use_unnormalized_coordinates);
  samplers_[key] = sampler;
  return sampler;
}

bool SamplerCache::Key::operator==(const SamplerCache::Key& other) const {
  return format == other.format && filter == other.filter &&
         use_unnormalized_coordinates == other.use_unnormalized_coordinates;
}

size_t SamplerCache::Key::Hash::operator()(const SamplerCache::Key& key) const {
  auto h1 = std::hash<vk::Format>()(key.format);
  auto h2 = std::hash<vk::Filter>()(key.filter);
  auto h3 = std::hash<bool>()(key.use_unnormalized_coordinates);
  return h1 ^ (RotateLeft(h2, 1)) ^ (RotateLeft(h3, 2));
}

}  // namespace escher
