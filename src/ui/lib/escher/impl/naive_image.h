// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_NAIVE_IMAGE_H_
#define SRC_UI_LIB_ESCHER_IMPL_NAIVE_IMAGE_H_

#include "src/ui/lib/escher/vk/image.h"

namespace escher {
namespace impl {

class NaiveImage : public Image {
 public:
  // Constructor. Claims ownership of the vk::Image, and binds it to the
  // provided GpuMemPtr.
  static ImagePtr AdoptVkImage(ResourceManager* image_owner, ImageInfo info, vk::Image image,
                               GpuMemPtr mem, vk::ImageLayout initial_layout);

  // Destroys image_ and releases the reference to mem_.
  ~NaiveImage() override;

  const GpuMemPtr& memory() const { return mem_; }

 private:
  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  NaiveImage(ResourceManager* image_owner, ImageInfo info, vk::Image image, GpuMemPtr mem,
             vk::ImageLayout initial_layout);

  const GpuMemPtr mem_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_NAIVE_IMAGE_H_
