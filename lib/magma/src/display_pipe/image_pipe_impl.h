// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_IMAGE_PIPE_IMPL_H_
#define MAGMA_IMAGE_PIPE_IMPL_H_

#include <unordered_map>

#include "lib/images/fidl/image_pipe.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "magma/src/display_pipe/image.h"

namespace display_pipe {

class ImagePipeImpl : public scenic::ImagePipe {
public:
    ImagePipeImpl(std::shared_ptr<MagmaConnection> conn);
    ~ImagePipeImpl() override;

    void AddImage(uint32_t image_id, scenic::ImageInfoPtr image_info, zx::vmo memory,
                  scenic::MemoryType memory_type, uint64_t memory_offset) override;
    void RemoveImage(uint32_t image_id) override;
    void PresentImage(uint32_t image_id, uint64_t presetation_time, zx::event acquire_fence,
                      zx::event release_fence, const PresentImageCallback& callback) override;

    void AddBinding(fidl::InterfaceRequest<ImagePipe> request);

private:
    std::shared_ptr<MagmaConnection> conn_;
    std::unordered_map<uint32_t, std::unique_ptr<Image>> images_;
    fidl::BindingSet<scenic::ImagePipe> bindings_;

    FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipeImpl);
};
} // namespace display_pipe

#endif // MAGMA_IMAGE_PIPE_IMPL_H_
