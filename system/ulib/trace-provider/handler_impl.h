// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace/handler.h>

#include <zx/eventpair.h>
#include <zx/vmo.h>
#include <fbl/macros.h>

namespace trace {
namespace internal {

class TraceHandlerImpl final : public trace::TraceHandler {
public:
    static zx_status_t StartEngine(async_t* async, zx::vmo buffer, zx::eventpair fence);
    static zx_status_t StopEngine();

private:
    TraceHandlerImpl(void* buffer, size_t buffer_num_bytes, zx::eventpair fence);
    ~TraceHandlerImpl() override;

    // |trace::TraceHandler|
    bool IsCategoryEnabled(const char* category) override;
    void TraceStopped(async_t* async,
                      zx_status_t disposition, size_t buffer_bytes_written) override;

    void* buffer_;
    size_t buffer_num_bytes_;
    zx::eventpair fence_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceHandlerImpl);
};

} // namespace internal
} // namespace trace
