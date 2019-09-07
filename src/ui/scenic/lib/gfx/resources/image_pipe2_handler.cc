// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe2_handler.h"

#include "src/ui/scenic/lib/gfx/resources/image_pipe2.h"

namespace scenic_impl {
namespace gfx {

ImagePipe2Handler::ImagePipe2Handler(::fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request,
                                     ::scenic_impl::gfx::ImagePipe2* image_pipe)
    : binding_(this, std::move(request)), image_pipe_(image_pipe) {
  binding_.set_error_handler([image_pipe](zx_status_t status) { image_pipe->OnConnectionError(); });
}

void ImagePipe2Handler::AddBufferCollection(
    uint32_t buffer_collection_id,
    ::fidl::InterfaceHandle<::fuchsia::sysmem::BufferCollectionToken> buffer_collection_token) {
  image_pipe_->AddBufferCollection(buffer_collection_id, std::move(buffer_collection_token));
}

void ImagePipe2Handler::AddImage(uint32_t image_id, uint32_t buffer_collection_id,
                                 uint32_t buffer_collection_index,
                                 ::fuchsia::sysmem::ImageFormat_2 image_format) {
  image_pipe_->AddImage(image_id, buffer_collection_id, buffer_collection_index, image_format);
}

void ImagePipe2Handler::RemoveBufferCollection(uint32_t buffer_collection_id) {
  image_pipe_->RemoveBufferCollection(buffer_collection_id);
}

void ImagePipe2Handler::RemoveImage(uint32_t image_id) { image_pipe_->RemoveImage(image_id); }

void ImagePipe2Handler::PresentImage(uint32_t image_id, uint64_t presentation_time,
                                     ::std::vector<::zx::event> acquire_fences,
                                     ::std::vector<::zx::event> release_fences,
                                     PresentImageCallback callback) {
  image_pipe_->PresentImage(image_id, zx::time(presentation_time), std::move(acquire_fences),
                            std::move(release_fences), std::move(callback));
}

}  // namespace gfx
}  // namespace scenic_impl
