// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scene/client/host_memory.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace mozart {
namespace skia {

// Creates a Skia surface backed by host-accessible shared memory associated
// with an image resource.
sk_sp<SkSurface> MakeSkSurface(const mozart::client::HostImage& image);

// Creates a Skia surface backed by host-accessible shared memory.
sk_sp<SkSurface> MakeSkSurface(const mozart2::ImageInfo& image_info,
                               ftl::RefPtr<mozart::client::HostData> data,
                               off_t memory_offset);
sk_sp<SkSurface> MakeSkSurface(SkImageInfo image_info,
                               size_t row_bytes,
                               ftl::RefPtr<mozart::client::HostData> data,
                               off_t memory_offset);

// Represents a pool of Skia surfaces and image resources backed by
// host-accessible shared memory bound to a session.  All images in the pool
// must have the same layout.
class HostSkSurfacePool {
 public:
  // Creates a pool which can supply up to |num_images| images on demand.
  explicit HostSkSurfacePool(mozart::client::Session* session,
                             uint32_t num_images = 2u);
  ~HostSkSurfacePool();

  // The number of images which this pool can manage.
  uint32_t num_images() const { return image_pool_.num_images(); }

  // Gets information about the images in the pool, or nullptr if the
  // pool is not configured.
  const mozart2::ImageInfo* image_info() const {
    return image_pool_.image_info();
  }

  // Sets the image information for images in the pool.
  // Previously created images are released but their memory may be reused.
  // If |image_info| is nullptr, the pool reverts to an non-configured state;
  // all images are released but the memory is retained for recycling.
  // Returns true if the configuration changed.
  bool Configure(mozart2::ImageInfoPtr image_info);

  // Gets the surface backed by the image with the specified index.
  // The |index| must be between 0 and |num_images() - 1|.
  // The returned pointer is valid until the image is discarded or the
  // pool is reconfigured.  Returns nullptr if the pool is not configured.
  sk_sp<SkSurface> GetSkSurface(uint32_t index);

  // Gets the image with the specified index.
  // The |index| must be between 0 and |num_images() - 1|.
  // The returned pointer is valid until the image is discarded or the
  // pool is reconfigured.  Returns nullptr if the pool is not configured.
  const mozart::client::HostImage* GetImage(uint32_t index) {
    return image_pool_.GetImage(index);
  }

  // Discards the image with the specified index but recycles its memory.
  // The |index| must be between 0 and |num_images() - 1|.
  void DiscardImage(uint32_t index) { image_pool_.DiscardImage(index); }

 private:
  mozart::client::HostImagePool image_pool_;
  std::vector<sk_sp<SkSurface>> surface_ptrs_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostSkSurfacePool);
};

}  // namespace skia
}  // namespace mozart
