// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SHAPE_ROUNDED_RECT_FACTORY_H_
#define SRC_UI_LIB_ESCHER_SHAPE_ROUNDED_RECT_FACTORY_H_

#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/shape/rounded_rect.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"

namespace escher {

class BatchGpuUploader;

class RoundedRectFactory : private ResourceRecycler {
 public:
  explicit RoundedRectFactory(EscherWeakPtr escher);
  ~RoundedRectFactory() override;

  MeshPtr NewRoundedRect(const RoundedRectSpec& spec, const MeshSpec& mesh_spec,
                         BatchGpuUploader* gpu_uploader);

 private:
  BufferPtr GetIndexBuffer(const RoundedRectSpec& spec,
                           const MeshSpec& mesh_spec,
                           BatchGpuUploader* gpu_uploader);

  BufferFactoryAdapter buffer_factory_;
  BufferPtr index_buffer_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SHAPE_ROUNDED_RECT_FACTORY_H_
