// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_

#include "garnet/lib/ui/gfx/resources/memory.h"
#include "src/ui/lib/escher/vk/buffer.h"

namespace scenic_impl {
namespace gfx {

// A resource that represents an escher::Buffer object. This class also keeps
// track of an optional backing resource for reporting purposes (e.g.,
// dump_visitor).
class Buffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Buffer(Session* session, ResourceId id, escher::GpuMemPtr gpu_mem,
         ResourcePtr backing_resource);

  void Accept(class ResourceVisitor* visitor) override;

  const ResourcePtr& backing_resource() { return backing_resource_; }
  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  vk::DeviceSize size() const { return escher_buffer_->size(); }

 private:
  ResourcePtr backing_resource_;
  escher::BufferPtr escher_buffer_;
};

using BufferPtr = fxl::RefPtr<Buffer>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
