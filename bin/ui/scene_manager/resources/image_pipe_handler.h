// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/images/fidl/image_pipe.fidl.h"

namespace scene_manager {

class ImagePipe;

class ImagePipeHandler : public scenic::ImagePipe {
 public:
  ImagePipeHandler(::fidl::InterfaceRequest<scenic::ImagePipe> request,
                   scene_manager::ImagePipe* image_pipe);

 private:
  void AddImage(uint32_t image_id,
                scenic::ImageInfoPtr image_info,
                zx::vmo memory,
                scenic::MemoryType memory_type,
                uint64_t memory_offset) override;
  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id,
                    uint64_t presentation_time,
                    ::fidl::Array<zx::event> acquire_fences,
                    ::fidl::Array<zx::event> release_fences,
                    const PresentImageCallback& callback) override;

  ::fidl::Binding<scenic::ImagePipe> binding_;
  scene_manager::ImagePipe* image_pipe_;
};

}  // namespace scene_manager
