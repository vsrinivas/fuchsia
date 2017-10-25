// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>
#include <zx/vmo.h>

#include "garnet/lib/magma/include/magma_abi/magma.h"

namespace scene_manager {

// Wraps a magma_connection_t and takes care of releasing it on object
// destruction.
class MagmaConnection {
 public:
  MagmaConnection();
  ~MagmaConnection();

  // Must be called before calling any of the methods below.
  bool Open();

  bool GetDisplaySize(uint32_t* width, uint32_t* height);
  bool ImportBuffer(const zx::vmo& vmo_handle, magma_buffer_t* buffer);
  void FreeBuffer(magma_buffer_t buffer);

  bool CreateSemaphore(magma_semaphore_t* sem);
  bool ImportSemaphore(const zx::event& event, magma_semaphore_t* sem);
  void ReleaseSemaphore(magma_semaphore_t sem);
  void SignalSemaphore(magma_semaphore_t sem);
  void ResetSemaphore(magma_semaphore_t sem);

  // Replace the currently-displayed framebuffer with |buffer|, after first
  // waiting for all of |wait_semaphores| to be signaled.  At this moment,
  // |buffer_presented_semaphore| is signaled.  When |buffer| is eventually
  // replaced by the next framebuffer, all of |signal_semaphores| will be
  // signaled.
  bool DisplayPageFlip(magma_buffer_t buffer,
                       uint32_t wait_semaphore_count,
                       const magma_semaphore_t* wait_semaphores,
                       uint32_t signal_semaphore_count,
                       const magma_semaphore_t* signal_semaphores,
                       magma_semaphore_t buffer_presented_semaphore);

 private:
  int fd_;
  magma_connection_t* conn_;
};

}  // namespace scene_manager
