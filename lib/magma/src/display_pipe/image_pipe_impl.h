// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_IMAGE_PIPE_IMPL_H_
#define MAGMA_IMAGE_PIPE_IMPL_H_

#include <unordered_map>

#include "apps/mozart/services/images/image_pipe.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "magma/src/display_pipe/image.h"

namespace display_pipe {

class ImagePipeImpl : public mozart2::ImagePipe {
 public:
  ImagePipeImpl(std::shared_ptr<MagmaConnection> conn);
  ~ImagePipeImpl() override;

  void AddImage(uint32_t image_id,
                mozart2::ImageInfoPtr image_info,
                mx::vmo memory, mozart2::MemoryType memory_type,
                uint64_t memory_offset) override;
  void RemoveImage(uint32_t image_id) override;
  void PresentImage(uint32_t image_id, mx::event acquire_fence,
                    mx::event release_fence) override;

  void AddBinding(fidl::InterfaceRequest<ImagePipe> request);

 private:
  std::shared_ptr<MagmaConnection> conn_;
  std::unordered_map<uint32_t, std::unique_ptr<Image>> images_;
  fidl::BindingSet<mozart2::ImagePipe> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImagePipeImpl);
};
}  // namespace display_pipe

#endif  // MAGMA_IMAGE_PIPE_IMPL_H_
