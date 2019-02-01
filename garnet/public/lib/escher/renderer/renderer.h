// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_RENDERER_H_
#define LIB_ESCHER_RENDERER_RENDERER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/renderer/semaphore.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

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

 private:
  const EscherWeakPtr escher_;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_RENDERER_H_
