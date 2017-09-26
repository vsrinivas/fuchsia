// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_MAGMA_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_MAGMA_CONNECTION_H_

#include <zx/event.h>
#include <zx/vmo.h>

#include "magma.h"

class MagmaConnection {
 public:
  MagmaConnection();
  ~MagmaConnection();

  bool Open();
  bool GetDisplaySize(uint32_t *width, uint32_t *height);
  bool ImportBuffer(const zx::vmo &vmo_handle, magma_buffer_t *buffer);
  void FreeBuffer(magma_buffer_t buffer);

  bool CreateSemaphore(magma_semaphore_t *sem);
  bool ImportSemaphore(const zx::event &event, magma_semaphore_t *sem);
  void ReleaseSemaphore(magma_semaphore_t sem);
  void SignalSemaphore(magma_semaphore_t sem);
  void ResetSemaphore(magma_semaphore_t sem);

  void DisplayPageFlip(magma_buffer_t buffer, uint32_t wait_semaphore_count,
                       const magma_semaphore_t* wait_semaphores, uint32_t signal_semaphore_count,
                       const magma_semaphore_t* signal_semaphores,
                       magma_semaphore_t buffer_presented_semaphore);

  private:
  int fd_;
  magma_connection_t *conn_;
};

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_MAGMA_CONNECTION_H_
