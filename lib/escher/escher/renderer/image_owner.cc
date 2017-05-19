// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/image_owner.h"

#include "escher/vk/gpu_mem.h"

namespace escher {

ImagePtr ImageOwner::CreateImage(std::unique_ptr<ImageCore> core) {
  return ftl::AdoptRef(new Image(std::move(core)));
}

}  // namespace escher
