// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_mmio.h"

#include <stdlib.h>

#include "magma_util/dlog.h"

std::unique_ptr<MockMmio> MockMmio::Create(uint64_t size) {
  void* addr = calloc(size, 1);
  return std::unique_ptr<MockMmio>(new MockMmio(addr, size));
}

MockMmio::MockMmio(void* addr, uint64_t size) : magma::PlatformMmio(addr, size) {}

MockMmio::~MockMmio() {
  DLOG("MockMmio dtor");
  free(addr());
}
