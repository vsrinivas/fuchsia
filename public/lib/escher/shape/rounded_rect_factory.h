// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/shape/rounded_rect.h"

namespace escher {

class BufferFactory;

class RoundedRectFactory : private ResourceRecycler {
 public:
  explicit RoundedRectFactory(Escher* escher);
  ~RoundedRectFactory() override;

  MeshPtr NewRoundedRect(const RoundedRectSpec& spec,
                         const MeshSpec& mesh_spec);

 private:
  BufferPtr GetIndexBuffer(const RoundedRectSpec& spec,
                           const MeshSpec& mesh_spec);

  std::unique_ptr<BufferFactory> buffer_factory_;
  impl::GpuUploader* const uploader_;

  BufferPtr index_buffer_;
};

}  // namespace escher
