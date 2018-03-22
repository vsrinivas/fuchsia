// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_

#include "lib/fidl/cpp/binding_set.h"
#include "lib/images/fidl/image_pipe.fidl.h"

namespace scenic {
namespace gfx {

class ImagePipe;

class ImagePipeHandler : public ui::gfx::ImagePipe {
 public:
  ImagePipeHandler(::f1dl::InterfaceRequest<ui::gfx::ImagePipe> request,
                   scenic::gfx::ImagePipe* image_pipe);

 private:
  void AddImage(uint32_t image_id,
                ui::gfx::ImageInfoPtr image_info,
                zx::vmo memory,
                ui::gfx::MemoryType memory_type,
                uint64_t memory_offset) override;
  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id,
                    uint64_t presentation_time,
                    ::f1dl::VectorPtr<zx::event> acquire_fences,
                    ::f1dl::VectorPtr<zx::event> release_fences,
                    const PresentImageCallback& callback) override;

  ::f1dl::Binding<ui::gfx::ImagePipe> binding_;
  scenic::gfx::ImagePipe* image_pipe_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
