// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/register_io.h"
#include <vector>

#ifndef REGISTER_TRACER_H
#define REGISTER_TRACER_H

class RegisterTracer : public magma::RegisterIo::Hook {
public:
    struct Operation {
        enum Type { WRITE32, READ32, READ64 };
        Type type;
        uint32_t offset;
        uint64_t val;
    };

    std::vector<Operation>& trace() { return trace_; }

    void Write32(uint32_t offset, uint32_t val) override
    {
        trace_.emplace_back(Operation{Operation::WRITE32, offset, val});
    }
    void Read32(uint32_t offset, uint32_t val) override
    {
        trace_.emplace_back(Operation{Operation::READ32, offset, val});
    }
    void Read64(uint32_t offset, uint64_t val) override
    {
        trace_.emplace_back(Operation{Operation::READ64, offset, val});
    }

private:
    std::vector<Operation> trace_;
};

#endif // REGISTER_TRACER_H
