// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/images/image_pipe.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace mozart {
namespace scene {

class ImagePipe;

class ImagePipeHandler : public mozart2::ImagePipe {
 public:
  ImagePipeHandler(::fidl::InterfaceRequest<mozart2::ImagePipe> request,
                   mozart::scene::ImagePipe* image_pipe);

 private:
  void AddImage(uint32_t image_id,
                mozart2::ImageInfoPtr image_info,
                mx::vmo memory,
                mozart2::MemoryType memory_type,
                uint64_t memory_offset) override;
  void RemoveImage(uint32_t image_id) override;
  // TODO(MZ-152): Add Presentation time to image_pipe.fidl.
  void PresentImage(uint32_t image_id,
                    uint64_t presentation_time,
                    mx::event acquire_fence,
                    mx::event release_fence,
                    const PresentImageCallback& callback) override;

  ::fidl::Binding<mozart2::ImagePipe> binding_;
  mozart::scene::ImagePipe* image_pipe_;
};

}  // namespace scene
}  // namespace mozart
