// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_
#define SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/semaphore.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

namespace escher {

class Renderer : public fxl::RefCountedThreadSafe<Renderer> {
 public:
  const VulkanContext& vulkan_context() { return context_; }

  Escher* escher() const { return escher_.get(); }
  EscherWeakPtr GetEscherWeakPtr() { return escher_; }

 protected:
  explicit Renderer(EscherWeakPtr escher);
  virtual ~Renderer();

  const VulkanContext context_;
  std::vector<TexturePtr> depth_buffers_;
  std::vector<TexturePtr> msaa_buffers_;

 private:
  const EscherWeakPtr escher_;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_
