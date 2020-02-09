// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_buffer.h"

#include <msd.h>

#include <magma_util/macros.h>

msd_buffer_t* msd_buffer_import(uint32_t handle) {
  auto buffer = magma::PlatformBuffer::Import(handle);
  if (!buffer)
    return DRETP(nullptr, "failed to import buffer handle 0x%x", handle);
  return new MsdQcomAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer_t* buffer) { delete MsdQcomAbiBuffer::cast(buffer); }
