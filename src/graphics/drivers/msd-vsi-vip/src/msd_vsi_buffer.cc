// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsi_buffer.h"

std::unique_ptr<MsdVsiBuffer> MsdVsiBuffer::Import(uint32_t handle) {
  auto platform_buf = magma::PlatformBuffer::Import(handle);
  if (!platform_buf)
    return DRETP(nullptr, "failed to import buffer handle 0x%x", handle);
  return std::make_unique<MsdVsiBuffer>(std::move(platform_buf));
}

std::unique_ptr<MsdVsiBuffer> MsdVsiBuffer::Create(uint64_t size, const char* name) {
  auto platform_buf = magma::PlatformBuffer::Create(size, name);
  if (!platform_buf)
    return DRETP(nullptr, "failed to create buffer size %lu", size);
  return std::make_unique<MsdVsiBuffer>(std::move(platform_buf));
}

//////////////////////////////////////////////////////////////////////////////

msd_buffer_t* msd_buffer_import(uint32_t handle) {
  auto buffer = MsdVsiBuffer::Import(handle);
  if (!buffer)
    return DRETP(nullptr, "failed to import buffer handle 0x%x", handle);
  return new MsdVsiAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer_t* buf) { delete MsdVsiAbiBuffer::cast(buf); }
