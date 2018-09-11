// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_

#include "garnet/lib/ui/gfx/resources/memory.h"
#include "lib/escher/vk/buffer.h"

namespace scenic_impl {
namespace gfx {

// Wraps a Vulkan buffer object.
class Buffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Buffer(Session* session, ResourceId id, MemoryPtr memory, uint32_t size,
         uint32_t offset);

  void Accept(class ResourceVisitor* visitor) override;

  const MemoryPtr& memory() const { return memory_; }
  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  vk::DeviceSize size() const { return escher_buffer_->size(); }

 private:
  MemoryPtr memory_;
  escher::BufferPtr escher_buffer_;
};

using BufferPtr = fxl::RefPtr<Buffer>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
