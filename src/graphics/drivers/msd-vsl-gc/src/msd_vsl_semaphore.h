// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_SEMAPHORE_H
#define MSD_VSL_SEMAPHORE_H

#include "magma_util/macros.h"
#include "msd.h"
#include "platform_semaphore.h"

class MsdVslAbiSemaphore : public msd_semaphore_t {
 public:
  MsdVslAbiSemaphore(std::shared_ptr<magma::PlatformSemaphore> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdVslAbiSemaphore* cast(msd_semaphore_t* semaphore) {
    DASSERT(semaphore);
    DASSERT(semaphore->magic_ == kMagic);
    return static_cast<MsdVslAbiSemaphore*>(semaphore);
  }

  std::shared_ptr<magma::PlatformSemaphore> ptr() { return ptr_; }

 private:
  std::shared_ptr<magma::PlatformSemaphore> ptr_;

  static constexpr uint32_t kMagic = 0x73656d61;  // "sema"
};

#endif  // MSD_VSL_SEMAPHORE_H
