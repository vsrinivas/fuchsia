// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/swapchain/magma_semaphore.h"

namespace scene_manager {

MagmaSemaphore::MagmaSemaphore() : magma_connection_(nullptr), semaphore_(0) {}

MagmaSemaphore::MagmaSemaphore(MagmaConnection* conn,
                               magma_semaphore_t semaphore)
    : magma_connection_(conn), semaphore_(semaphore) {}

MagmaSemaphore::MagmaSemaphore(MagmaSemaphore&& rhs)
    : magma_connection_(rhs.magma_connection_), semaphore_(rhs.semaphore_) {
  rhs.magma_connection_ = nullptr;
  rhs.semaphore_ = 0;
}

MagmaSemaphore& MagmaSemaphore::operator=(MagmaSemaphore&& rhs) {
  magma_connection_ = rhs.magma_connection_;
  semaphore_ = rhs.semaphore_;
  rhs.magma_connection_ = nullptr;
  rhs.semaphore_ = 0;
  return *this;
}

MagmaSemaphore::~MagmaSemaphore() {
  if (magma_connection_ != nullptr && semaphore_ != 0) {
    magma_connection_->ReleaseSemaphore(semaphore_);
  }
}

MagmaSemaphore MagmaSemaphore::NewFromEvent(MagmaConnection* magma_connection,
                                            const zx::event& event) {
  magma_semaphore_t semaphore;

  bool success = magma_connection->ImportSemaphore(event, &semaphore);
  return success ? MagmaSemaphore(magma_connection, semaphore)
                 : MagmaSemaphore();
}

}  // namespace scene_manager
