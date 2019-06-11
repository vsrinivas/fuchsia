// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** PT-127 ****************************************************************
// This file is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TRACELINK_PROVIDER_IMPL_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TRACELINK_PROVIDER_IMPL_H_

#include <string>
#include <vector>

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <trace-engine/types.h>
#include <trace-provider/tracelink_provider.h>

// Provide a definition for the opaque type declared in tracelink_provider.h.
struct tracelink_provider {};

namespace trace {
namespace internal {

class TracelinkProviderImpl final : public tracelink_provider_t {
public:
    TracelinkProviderImpl(async_dispatcher_t* dispatcher, zx::channel channel);
    ~TracelinkProviderImpl();

private:
    class Connection final {
    public:
        Connection(TracelinkProviderImpl* impl, zx::channel channel);
        ~Connection();

    private:
        void Handle(async_dispatcher_t* dispatcher,
                    async::WaitBase* wait,
                    zx_status_t status,
                    const zx_packet_signal_t* signal);

        bool ReadMessage();
        bool DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes,
                               zx_handle_t* handles, uint32_t num_handles);
        void Close();

        TracelinkProviderImpl* const impl_;
        zx::channel channel_;
        async::WaitMethod<Connection, &Connection::Handle> wait_;
    };

    void Start(trace_buffering_mode_t buffering_mode, zx::vmo buffer,
               zx::fifo fifo, std::vector<std::string> categories);
    void Stop();
    void OnClose();

    async_dispatcher_t* const dispatcher_;
    Connection connection_;

    TracelinkProviderImpl(const TracelinkProviderImpl&) = delete;
    TracelinkProviderImpl(TracelinkProviderImpl&&) = delete;
    TracelinkProviderImpl& operator=(const TracelinkProviderImpl&) = delete;
    TracelinkProviderImpl& operator=(TracelinkProviderImpl&&) = delete;
};

} // namespace internal
} // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_TRACELINK_PROVIDER_IMPL_H_
