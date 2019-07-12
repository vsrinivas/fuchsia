// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_MMIO_H
#define MOCK_MMIO_H

#include <memory>

#include "platform_mmio.h"

class MockMmio : public magma::PlatformMmio {
 public:
  static std::unique_ptr<MockMmio> Create(uint64_t size);

  virtual ~MockMmio() override;

  uint64_t physical_address() override { return 0; }

 private:
  MockMmio(void* addr, uint64_t size);
};

#endif  // MOCK_MMIO_H
