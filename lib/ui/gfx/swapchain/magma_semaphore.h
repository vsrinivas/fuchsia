// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SWAPCHAIN_MAGMA_SEMAPHORE_H_
#define GARNET_LIB_UI_GFX_SWAPCHAIN_MAGMA_SEMAPHORE_H_

#include <memory>

#include <zx/event.h>

#include "garnet/lib/ui/gfx/swapchain/magma_connection.h"
#include "lib/fxl/macros.h"

namespace scenic {
namespace gfx {

// Wraps a magma_semaphore_t and takes care of releasing it on object
// destruction.
class MagmaSemaphore {
 public:
  MagmaSemaphore();
  MagmaSemaphore(MagmaConnection* magma_connection,
                 magma_semaphore_t semaphore);
  MagmaSemaphore(MagmaSemaphore&& rhs);
  MagmaSemaphore& operator=(MagmaSemaphore&& rhs);

  ~MagmaSemaphore();

  static MagmaSemaphore NewFromEvent(MagmaConnection* magma_connection,
                                     const zx::event& event);
  const magma_semaphore_t& get() const { return semaphore_; }

  operator bool() const { return semaphore_; }

 private:
  MagmaConnection* magma_connection_;
  magma_semaphore_t semaphore_;
  zx::vmo vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MagmaSemaphore);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_SWAPCHAIN_MAGMA_SEMAPHORE_H_
