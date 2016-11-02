// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_MMIO_H
#define MOCK_MMIO_H

#include "platform_mmio.h"
#include <memory>

class MockMmio : public magma::PlatformMmio {
public:
    static std::unique_ptr<MockMmio> Create(uint64_t size);

    virtual ~MockMmio() override;

private:
    MockMmio(void* addr, uint64_t size);
};

#endif // MOCK_MMIO_H
