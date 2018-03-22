// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_

#include <unordered_map>

#include "lib/images/fidl/image_pipe.fidl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "garnet/lib/magma/src/display_pipe/image.h"

namespace display_pipe {

class ImagePipeImpl : public ui::gfx::ImagePipe {
public:
    ImagePipeImpl(std::shared_ptr<MagmaConnection> conn);
    ~ImagePipeImpl() override;

    void AddImage(uint32_t image_id, ui::gfx::ImageInfoPtr image_info, zx::vmo memory,
                  ui::gfx::MemoryType memory_type, uint64_t memory_offset) override;
    void RemoveImage(uint32_t image_id) override;
    void PresentImage(uint32_t image_id,
                      uint64_t presetation_time,
                      ::f1dl::VectorPtr<zx::event> acquire_fences,
                      ::f1dl::VectorPtr<zx::event> release_fences,
                      const PresentImageCallback& callback) override;

    void AddBinding(f1dl::InterfaceRequest<ImagePipe> request);

private:
    std::shared_ptr<MagmaConnection> conn_;
    std::unordered_map<uint32_t, std::unique_ptr<Image>> images_;
    f1dl::BindingSet<ui::gfx::ImagePipe> bindings_;

    FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipeImpl);
};
} // namespace display_pipe

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_
