// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

namespace scenic_impl {
namespace gfx {

ImagePipeHandler::ImagePipeHandler(fidl::InterfaceRequest<fuchsia::images::ImagePipe> request,
                                   ::scenic_impl::gfx::ImagePipe* image_pipe)
    : binding_(this, std::move(request)), image_pipe_(image_pipe) {
  binding_.set_error_handler([image_pipe](zx_status_t status) { image_pipe->OnConnectionError(); });
}

void ImagePipeHandler::AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                                zx::vmo memory, uint64_t offset_bytes, uint64_t size_bytes,
                                fuchsia::images::MemoryType memory_type) {
  image_pipe_->AddImage(image_id, std::move(image_info), std::move(memory), offset_bytes,
                        size_bytes, memory_type);
}

void ImagePipeHandler::RemoveImage(uint32_t image_id) { image_pipe_->RemoveImage(image_id); }

void ImagePipeHandler::PresentImage(uint32_t image_id, uint64_t presentation_time,
                                    std::vector<zx::event> acquire_fences,
                                    std::vector<zx::event> release_fences,
                                    PresentImageCallback callback) {
  image_pipe_->PresentImage(image_id, zx::time(presentation_time), std::move(acquire_fences),
                            std::move(release_fences), std::move(callback));
}

}  // namespace gfx
}  // namespace scenic_impl
