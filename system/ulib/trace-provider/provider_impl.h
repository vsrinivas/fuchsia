// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>
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
        void Handle(async_t* async,
                    async::WaitBase* wait,
                    zx_status_t status,
                    const zx_packet_signal_t* signal);

        bool ReadMessage();
        bool DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes,
                               zx_handle_t* handles, uint32_t num_handles);
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
