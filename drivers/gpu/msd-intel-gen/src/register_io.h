// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTER_IO_H
#define REGISTER_IO_H

#include "magma_util/platform/platform_mmio.h"
#include <memory>

// RegisterIo wraps mmio access and adds forcewake logic.
class RegisterIo {
public:
    RegisterIo(std::unique_ptr<magma::PlatformMmio> mmio);

    void Write32(uint32_t offset, uint32_t val) { mmio_->Write32(val, offset); }

    uint32_t Read32(uint32_t offset) { return mmio_->Read32(offset); }

    uint64_t Read64(uint32_t offset) { return mmio_->Read64(offset); }

    magma::PlatformMmio* mmio() { return mmio_.get(); }

private:
    std::unique_ptr<magma::PlatformMmio> mmio_;
};

#endif // REGISTER_IO_H
