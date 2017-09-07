// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/wait.h>
#include <mx/channel.h>
#include <mx/eventpair.h>
#include <mx/vmo.h>
#include <fbl/macros.h>
#include <trace-provider/provider.h>

// Provide a definition for the opaque type declared in provider.h.
struct trace_provider {};

namespace trace {
namespace internal {

class TraceProviderImpl final : public trace_provider_t {
public:
    TraceProviderImpl(async_t* async, mx::channel channel);
    ~TraceProviderImpl();

private:
    class Connection final {
    public:
        Connection(TraceProviderImpl* impl, mx::channel channel);
        ~Connection();

    private:
        async_wait_result_t Handle(async_t* async,
                                   mx_status_t status,
                                   const mx_packet_signal_t* signal);

        bool ReadMessage();
        void Close();

        TraceProviderImpl* const impl_;
        mx::channel channel_;
        async::Wait wait_;
    };

    bool Start(mx::vmo buffer, mx::eventpair fence);
    void Stop();

    async_t* const async_;
    Connection connection_;
    bool running_ = false;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceProviderImpl);
};

} // namespace internal
} // namespace trace
