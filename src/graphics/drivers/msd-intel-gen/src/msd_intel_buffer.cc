// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_buffer.h"

#include "address_space.h"
#include "gpu_mapping.h"
#include "msd.h"

MsdIntelBuffer::MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
    : platform_buf_(std::move(platform_buf)) {}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Import(uint32_t handle) {
  auto platform_buf = magma::PlatformBuffer::Import(handle);
  if (!platform_buf)
    return DRETP(nullptr, "MsdIntelBuffer::Create: Could not create platform buffer from token");

  return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Create(uint64_t size, const char* name) {
  auto platform_buf = magma::PlatformBuffer::Create(size, name);
  if (!platform_buf)
    return DRETP(nullptr, "MsdIntelBuffer::Create: Could not create platform buffer from size");

  return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

//////////////////////////////////////////////////////////////////////////////

msd_buffer_t* msd_buffer_import(uint32_t handle) {
  auto buffer = MsdIntelBuffer::Import(handle);
  if (!buffer)
    return DRETP(nullptr, "MsdIntelBuffer::Create failed");
  return new MsdIntelAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer_t* buf) { delete MsdIntelAbiBuffer::cast(buf); }
