// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/image_owner.h"

#include "escher/impl/gpu_mem.h"

namespace escher {

ImagePtr ImageOwner::CreateImage(const ImageInfo& info,
                                 vk::Image image,
                                 impl::GpuMemPtr mem) {
  return ftl::AdoptRef(new Image(info, image, std::move(mem), this));
}

}  // namespace escher
