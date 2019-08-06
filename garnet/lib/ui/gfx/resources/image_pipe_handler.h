// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/time.h>

#include "lib/fidl/cpp/binding_set.h"

namespace scenic_impl {
namespace gfx {

class ImagePipe;

class ImagePipeHandler : public fuchsia::images::ImagePipe {
 public:
  ImagePipeHandler(::fidl::InterfaceRequest<fuchsia::images::ImagePipe> request,
                   ::scenic_impl::gfx::ImagePipe* image_pipe);

 private:
  // |fuchsia::images::ImagePipe|
  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info, zx::vmo memory,
                uint64_t offset_bytes, uint64_t size_bytes,
                fuchsia::images::MemoryType memory_type) override;

  // |fuchsia::images::ImagePipe|
  void RemoveImage(uint32_t image_id) override;

  // |fuchsia::images::ImagePipe|
  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    ::std::vector<zx::event> acquire_fences,
                    ::std::vector<zx::event> release_fences,
                    PresentImageCallback callback) override;

  ::fidl::Binding<fuchsia::images::ImagePipe> binding_;
  ::scenic_impl::gfx::ImagePipe* image_pipe_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_IMAGE_PIPE_HANDLER_H_
