// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace/handler.h>

#include <mx/eventpair.h>
#include <mx/vmo.h>
#include <fbl/macros.h>

namespace trace {
namespace internal {

class TraceHandlerImpl final : public trace::TraceHandler {
public:
    static mx_status_t StartEngine(async_t* async, mx::vmo buffer, mx::eventpair fence);
    static mx_status_t StopEngine();

private:
    TraceHandlerImpl(void* buffer, size_t buffer_num_bytes, mx::eventpair fence);
    ~TraceHandlerImpl() override;

    // |trace::TraceHandler|
    bool IsCategoryEnabled(const char* category) override;
    void TraceStopped(async_t* async,
                      mx_status_t disposition, size_t buffer_bytes_written) override;

    void* buffer_;
    size_t buffer_num_bytes_;
    mx::eventpair fence_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceHandlerImpl);
};

} // namespace internal
} // namespace trace
