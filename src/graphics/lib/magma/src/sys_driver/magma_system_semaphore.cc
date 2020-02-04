// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_semaphore.h"

#include "magma_util/macros.h"

MagmaSystemSemaphore::MagmaSystemSemaphore(
    std::unique_ptr<magma::PlatformSemaphore> platform_semaphore,
    msd_semaphore_unique_ptr_t msd_semaphore_t)
    : platform_semaphore_(std::move(platform_semaphore)),
      msd_semaphore_(std::move(msd_semaphore_t)) {}

std::unique_ptr<MagmaSystemSemaphore> MagmaSystemSemaphore::Create(
    std::unique_ptr<magma::PlatformSemaphore> platform_semaphore) {
  if (!platform_semaphore)
    return DRETP(nullptr, "null platform semaphore");

  uint32_t handle;
  if (!platform_semaphore->duplicate_handle(&handle))
    return DRETP(nullptr, "failed to get duplicate handle");

  msd_semaphore_t* msd_semaphore;
  magma_status_t status = msd_semaphore_import(handle, &msd_semaphore);
  if (status != MAGMA_STATUS_OK)
    return DRETP(nullptr, "msd_semaphore_import failed: %d", status);

  return std::unique_ptr<MagmaSystemSemaphore>(new MagmaSystemSemaphore(
      std::move(platform_semaphore), MsdSemaphoreUniquePtr(msd_semaphore)));
}
