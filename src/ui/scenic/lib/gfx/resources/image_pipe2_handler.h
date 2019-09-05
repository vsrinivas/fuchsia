// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_HANDLER_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_HANDLER_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/time.h>

#include "lib/fidl/cpp/binding_set.h"

namespace scenic_impl::gfx {

class ImagePipe2;

class ImagePipe2Handler : public fuchsia::images::ImagePipe2 {
 public:
  ImagePipe2Handler(::fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request,
                    ::scenic_impl::gfx::ImagePipe2* image_pipe);

 private:
  // fuchsia::images::ImagePipe2 implementation
  void AddBufferCollection(uint32_t buffer_collection_id,
                           ::fidl::InterfaceHandle<::fuchsia::sysmem::BufferCollectionToken>
                               buffer_collection_token) override;

  // fuchsia::images::ImagePipe2 implementation
  void AddImage(uint32_t image_id, uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                fuchsia::sysmem::ImageFormat_2 image_format) override;

  // fuchsia::images::ImagePipe2 implementation
  void RemoveBufferCollection(uint32_t buffer_collection_id) override;

  // fuchsia::images::ImagePipe2 implementation
  void RemoveImage(uint32_t image_id) override;

  // fuchsia::images::ImagePipe2 implementation
  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    ::std::vector<::zx::event> acquire_fences,
                    ::std::vector<::zx::event> release_fences,
                    PresentImageCallback callback) override;

  ::fidl::Binding<fuchsia::images::ImagePipe2> binding_;
  ::scenic_impl::gfx::ImagePipe2* image_pipe_;
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_HANDLER_H_
