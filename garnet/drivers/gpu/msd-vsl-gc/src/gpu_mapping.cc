// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping.h"

#include "address_space.h"

GpuMapping::~GpuMapping() {
  std::shared_ptr<AddressSpaceBase> address_space = address_space_.lock();
  if (!address_space) {
    DLOG("Failed to lock address space");
    return;
  }

  if (!address_space->Clear(gpu_addr_, page_count()))
    DLOG("failed to clear address");
}
