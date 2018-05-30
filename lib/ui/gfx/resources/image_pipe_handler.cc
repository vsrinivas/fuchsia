// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/image_pipe.h"

namespace scenic {
namespace gfx {

ImagePipeHandler::ImagePipeHandler(
    ::fidl::InterfaceRequest<fuchsia::images::ImagePipe> request,
    scenic::gfx::ImagePipe* image_pipe)
    : binding_(this, std::move(request)), image_pipe_(image_pipe) {
  binding_.set_error_handler([image_pipe] { image_pipe->OnConnectionError(); });
}

void ImagePipeHandler::AddImage(uint32_t image_id,
                                fuchsia::images::ImageInfo image_info,
                                zx::vmo memory,
                                fuchsia::images::MemoryType memory_type,
                                uint64_t memory_offset) {
  image_pipe_->AddImage(image_id, std::move(image_info), std::move(memory),
                        memory_type, memory_offset);
}

void ImagePipeHandler::RemoveImage(uint32_t image_id) {
  image_pipe_->RemoveImage(image_id);
}

void ImagePipeHandler::PresentImage(uint32_t image_id,
                                    uint64_t presentation_time,
                                    ::fidl::VectorPtr<zx::event> acquire_fences,
                                    ::fidl::VectorPtr<zx::event> release_fences,
                                    PresentImageCallback callback) {
  image_pipe_->PresentImage(image_id, presentation_time,
                            std::move(acquire_fences),
                            std::move(release_fences), callback);
}

}  // namespace gfx
}  // namespace scenic
