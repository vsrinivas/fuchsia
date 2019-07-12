// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_buffer.h"

#include "magma_util/macros.h"

MagmaSystemBuffer::MagmaSystemBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf,
                                     msd_buffer_unique_ptr_t msd_buf)
    : platform_buf_(std::move(platform_buf)), msd_buf_(std::move(msd_buf)) {}

std::unique_ptr<MagmaSystemBuffer> MagmaSystemBuffer::Create(
    std::unique_ptr<magma::PlatformBuffer> platform_buffer) {
  if (!platform_buffer)
    return DRETP(nullptr, "Failed to create PlatformBuffer");

  uint32_t duplicate_handle;
  if (!platform_buffer->duplicate_handle(&duplicate_handle))
    return DRETP(nullptr, "failed to get duplicate_handle");

  msd_buffer_unique_ptr_t msd_buf = MsdBufferUniquePtr(msd_buffer_import(duplicate_handle));
  if (!msd_buf)
    return DRETP(nullptr, "Failed to import newly allocated buffer into the MSD Implementation");

  return std::unique_ptr<MagmaSystemBuffer>(
      new MagmaSystemBuffer(std::move(platform_buffer), std::move(msd_buf)));
}
