// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_PROVIDER_IMPL_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_PROVIDER_IMPL_H_

#include <string>
#include <vector>

#include <lib/async/cpp/wait.h>
#include <lib/trace-engine/handler.h>
#include <lib/trace-engine/types.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

// Provide a definition for the opaque type declared in provider.h.
struct trace_provider {};

namespace trace {
namespace internal {

class TraceProviderImpl final : public trace_provider_t {
public:
    TraceProviderImpl(async_dispatcher_t* dispatcher, zx::channel channel);
    ~TraceProviderImpl();

private:
    class Connection final {
    public:
        Connection(TraceProviderImpl* impl, zx::channel channel);
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

        TraceProviderImpl* const impl_;
        zx::channel channel_;
        async::WaitMethod<Connection, &Connection::Handle> wait_;
    };

    void Initialize(trace_buffering_mode_t buffering_mode, zx::vmo buffer,
                    zx::fifo fifo, std::vector<std::string> categories);
    void Start(trace_start_mode_t start_mode,
               std::vector<std::string> additional_categories);

    void Stop();
    void Terminate();

    void OnStopped();
    void OnTerminated();
    void OnClose();

    async_dispatcher_t* const dispatcher_;
    Connection connection_;

    TraceProviderImpl(const TraceProviderImpl&) = delete;
    TraceProviderImpl(TraceProviderImpl&&) = delete;
    TraceProviderImpl& operator=(const TraceProviderImpl&) = delete;
    TraceProviderImpl& operator=(TraceProviderImpl&&) = delete;
};

} // namespace internal
} // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_PROVIDER_IMPL_H_
