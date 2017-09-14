// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/image_pipe.h"

namespace scene_manager {

ImagePipeHandler::ImagePipeHandler(
    ::fidl::InterfaceRequest<scenic::ImagePipe> request,
    scene_manager::ImagePipe* image_pipe)
    : binding_(this, std::move(request)), image_pipe_(image_pipe) {
  binding_.set_connection_error_handler(
      [image_pipe] { image_pipe->OnConnectionError(); });
}

void ImagePipeHandler::AddImage(uint32_t image_id,
                                scenic::ImageInfoPtr image_info,
                                zx::vmo memory,
                                scenic::MemoryType memory_type,
                                uint64_t memory_offset) {
  image_pipe_->AddImage(image_id, std::move(image_info), std::move(memory),
                        memory_type, memory_offset);
}

void ImagePipeHandler::RemoveImage(uint32_t image_id) {
  image_pipe_->RemoveImage(image_id);
}

void ImagePipeHandler::PresentImage(uint32_t image_id,
                                    uint64_t presentation_time,
                                    zx::event acquire_fence,
                                    zx::event release_fence,
                                    const PresentImageCallback& callback) {
  image_pipe_->PresentImage(image_id, presentation_time,
                            std::move(acquire_fence), std::move(release_fence),
                            callback);
}

}  // namespace scene_manager
