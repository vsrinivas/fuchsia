// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_

#include <unordered_map>

#include <fuchsia/cpp/images.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "garnet/lib/magma/src/display_pipe/image.h"

namespace display_pipe {

class ImagePipeImpl : public images::ImagePipe {
public:
    ImagePipeImpl(std::shared_ptr<MagmaConnection> conn);
    ~ImagePipeImpl() override;

    void AddImage(uint32_t image_id, images::ImageInfo image_info, zx::vmo memory,
                  images::MemoryType memory_type, uint64_t memory_offset) override;
    void RemoveImage(uint32_t image_id) override;
    void PresentImage(uint32_t image_id,
                      uint64_t presetation_time,
                      ::fidl::VectorPtr<zx::event> acquire_fences,
                      ::fidl::VectorPtr<zx::event> release_fences,
                      PresentImageCallback callback) override;

    void AddBinding(fidl::InterfaceRequest<ImagePipe> request);

private:
    std::shared_ptr<MagmaConnection> conn_;
    std::unordered_map<uint32_t, std::unique_ptr<Image>> images_;
    fidl::BindingSet<images::ImagePipe> bindings_;

    FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipeImpl);
};
} // namespace display_pipe

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_PIPE_IMPL_H_
