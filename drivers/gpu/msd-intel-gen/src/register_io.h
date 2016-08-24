// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTER_IO_H
#define REGISTER_IO_H

#include "magma_util/platform/platform_mmio.h"
#include "magma_util/dlog.h"
#include <memory>
#include <vector>

// RegisterIo wraps mmio access and adds forcewake logic.
class RegisterIo {
public:
    RegisterIo(std::unique_ptr<magma::PlatformMmio> mmio);

    void Write32(uint32_t offset, uint32_t val)
    {
        if (trace_enable_)
            trace_.emplace_back(Operation{Operation::WRITE32, offset, val});
        mmio_->Write32(val, offset);
    }

    uint32_t Read32(uint32_t offset)
    {
        uint32_t val = mmio_->Read32(offset);
        if (trace_enable_)
            trace_.emplace_back(Operation{Operation::READ32, offset, val});
        return val;
    }

    uint64_t Read64(uint32_t offset)
    {
        uint64_t val = mmio_->Read64(offset);
        if (trace_enable_)
            trace_.emplace_back(Operation{Operation::READ64, offset, val});
        return val;
    }

    magma::PlatformMmio* mmio() { return mmio_.get(); }

    struct Operation {
        enum Type { WRITE32, READ32, READ64 };
        Type type;
        uint32_t offset;
        uint64_t val;
    };

    std::vector<Operation>& trace() { return trace_; }

    void enable_trace(bool enable)
    {
        DLOG("enable_trace: %d", enable);
        trace_enable_ = enable;
    }

private:
    std::unique_ptr<magma::PlatformMmio> mmio_;
    std::vector<Operation> trace_;
    bool trace_enable_ = false;
};

#endif // REGISTER_IO_H
