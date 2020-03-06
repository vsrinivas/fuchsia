// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_SAMPLER_CACHE_H_
#define SRC_UI_LIB_ESCHER_RENDERER_SAMPLER_CACHE_H_

#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/vk/sampler.h"

namespace escher {

// SamplerCache lazily creates and caches Samplers upon demand.  These samplers are never released.
class SamplerCache final {
 public:
  explicit SamplerCache(fxl::WeakPtr<ResourceRecycler> resource_recycler);

  SamplerPtr ObtainSampler(vk::Filter filter, bool use_unnormalized_coordinates = false);
  SamplerPtr ObtainYuvSampler(vk::Format format, vk::Filter filter,
                              bool use_unnormalized_coordinates = false);

  // Return the number of samplers in the cache.
  size_t size() const { return samplers_.size(); }

 private:
  struct Key {
    vk::Format format;
    vk::Filter filter;
    bool use_unnormalized_coordinates;

    // Key equality and hashing, for storage in |samplers_|.
    bool operator==(const Key& other) const;
    struct Hash {
      size_t operator()(const Key& key) const;
    };
  };

  SamplerPtr ObtainSampler(const Key& key);

  std::unordered_map<Key, SamplerPtr, Key::Hash> samplers_;

  fxl::WeakPtr<ResourceRecycler> resource_recycler_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_SAMPLER_CACHE_H_
