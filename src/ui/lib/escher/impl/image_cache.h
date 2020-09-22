// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_IMAGE_CACHE_H_
#define SRC_UI_LIB_ESCHER_IMPL_IMAGE_CACHE_H_

#include <queue>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/image_factory.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace impl {

// Allow client to obtain new or recycled Images.  All Images obtained from an
// ImageCache must be destroyed before the ImageCache is destroyed.
//
// TODO(fxbug.dev/23725): this does not prune entries!!  Once a new Image is created, it
// will live until the cache is destroyed!!
class ImageCache : public ImageFactory, private ResourceManager {
 public:
  // The allocator is used to allocate memory for newly-created images.
  ImageCache(EscherWeakPtr escher, GpuAllocator* allocator);
  ~ImageCache() override;

  // |ImageFactory|
  //
  // Obtain an unused Image with the required properties.  A new Image might be
  // created, or an existing one reused.
  ImagePtr NewImage(const ImageInfo& info, GpuMemPtr* out_ptr = nullptr) override;

 private:
  // |Owner|
  //
  //  Adds the image to unused_images_.
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  // Try to find an unused image that meets the required specs.  If successful,
  // remove and return it.  Otherwise, return nullptr.
  ImagePtr FindImage(const ImageInfo& info);

  GpuAllocator* allocator_;

  // Keep track of all images that are available for reuse.
  // TODO: need some method of trimming the cache, to free images that haven't
  // been used.  Note: using a FIFO queue below (instead of a stack) will make
  // this more difficult, but there is a reason to do so: some of these images
  // may still be referenced by a pending command buffer, and reusing them in
  // FIFO order makes it less likely that a pipeline barrier will result in a
  // GPU stall.  The right approach is probably to trim the cache in the most
  // straightforward way, and profile to determine whether GPU stalls are a real
  // concern.
  HashMap<ImageInfo, std::queue<std::unique_ptr<Image>>> unused_images_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImageCache);
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_IMAGE_CACHE_H_
