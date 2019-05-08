// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_BINDING_H_
#define SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_BINDING_H_

#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {

// Struct that describes where to bind a range of uniform data.  UniformBinding
// is often used as per-frame data in a RenderQueue.  In such cases, it is
// common to allocate the struct itself via Frame::Allocate<UniformBinding>(),
// and to allocate the uniform data to bind via Frame::AllocateUniform().
struct UniformBinding {
  uint32_t descriptor_set_index = 0;
  uint32_t binding_index = 0;
  // Holds a Buffer* instead of a vk::Buffer because the CommandBuffer needs
  // the buffer UID to look up cached descriptor sets.
  Buffer* buffer = nullptr;
  vk::DeviceSize offset = 0;
  vk::DeviceSize size = 0;

  void Bind(CommandBuffer* cb) const {
    cb->BindUniformBuffer(descriptor_set_index, binding_index, buffer, offset,
                          size);
  }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_BINDING_H_
