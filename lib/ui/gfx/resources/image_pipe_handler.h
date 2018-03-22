// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_

#include <fuchsia/cpp/images.h>
#include "lib/fidl/cpp/binding_set.h"

namespace scenic {
namespace gfx {

class ImagePipe;

class ImagePipeHandler : public images::ImagePipe {
 public:
  ImagePipeHandler(::fidl::InterfaceRequest<images::ImagePipe> request,
                   scenic::gfx::ImagePipe* image_pipe);

 private:
  void AddImage(uint32_t image_id,
                images::ImageInfo image_info,
                zx::vmo memory,
                images::MemoryType memory_type,
                uint64_t memory_offset) override;
  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id,
                    uint64_t presentation_time,
                    ::fidl::VectorPtr<zx::event> acquire_fences,
                    ::fidl::VectorPtr<zx::event> release_fences,
                    PresentImageCallback callback) override;

  ::fidl::Binding<images::ImagePipe> binding_;
  scenic::gfx::ImagePipe* image_pipe_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
