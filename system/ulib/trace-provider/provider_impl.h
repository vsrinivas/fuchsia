// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/cpp/wait.h>
#include <zx/channel.h>
#include <zx/eventpair.h>
#include <zx/vmo.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <trace-provider/provider.h>

// Provide a definition for the opaque type declared in provider.h.
struct trace_provider {};

namespace trace {
namespace internal {

class TraceProviderImpl final : public trace_provider_t {
public:
    TraceProviderImpl(async_t* async, zx::channel channel);
    ~TraceProviderImpl();

private:
    class Connection final {
    public:
        Connection(TraceProviderImpl* impl, zx::channel channel);
        ~Connection();

    private:
        async_wait_result_t Handle(async_t* async,
                                   zx_status_t status,
                                   const zx_packet_signal_t* signal);

        bool ReadMessage();
        void Close();

        TraceProviderImpl* const impl_;
        zx::channel channel_;
        async::WaitMethod<Connection, &Connection::Handle> wait_;
    };

    void Start(zx::vmo buffer, zx::eventpair fence,
               fbl::Vector<fbl::String> enabled_categories);
    void Stop();

    async_t* const async_;
    Connection connection_;
    bool running_ = false;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceProviderImpl);
};

} // namespace internal
} // namespace trace
