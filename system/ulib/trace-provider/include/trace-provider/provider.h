// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The API for initializing the trace provider for a process.
//

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// The format of fifo packets for messages passed between the trace manager
// and trace providers.
typedef struct trace_provider_packet {
    // One of TRACE_PROVIDER_*.
    uint16_t request;

    // For alignment and future concerns, must be zero.
    uint16_t reserved;

    // Optional data for the request.
    // The contents depend on the request.
    // If unused they must be passed as zero.
    uint32_t data32;
    uint64_t data64;
} trace_provider_packet_t;

// The protocol version we are using.
// This is non-zero to catch initialization bugs.
#define TRACE_PROVIDER_FIFO_PROTOCOL_VERSION 1

// Provider->Manager
// Zero is reserved to catch initialization bugs.

// Indicate the provider successfully started.
// |data32| is TRACE_PROVIDER_FIFO_PROTOCOL_VERSION
// |data64| is unused (must be zero).
#define TRACE_PROVIDER_STARTED (0x1)

// Provider->Manager
// The buffer is full and at least one packet was dropped.
// |data32,data64| are unused (must be zero).
#define TRACE_PROVIDER_BUFFER_OVERFLOW (0x2)

// Next Provider->Manager packet = 0x2

// There are currently no packets going the other way (Manager->Provider).
// Next Manager->Provider packet = 0x100

// End fifo packet descriptions.

// Represents a trace provider.
typedef struct trace_provider trace_provider_t;

// Creates a trace provider associated with the specified async dispatcher
// and registers it with the tracing system.
//
// The trace provider will start and stop the trace engine in response to requests
// from the tracing system.
//
// |dispatcher| is the asynchronous dispatcher which the trace provider and trace
// engine will use for dispatch.  This must outlive the trace provider instance.
//
// Returns the trace provider, or null if creation failed.
//
// TODO(ZX-1036): Currently this connects to the trace manager service.
// Switch to passively exporting the trace provider via the "hub" through
// the process's exported directory once that stuff is implemented.  We'll
// probably need to pass some extra parameters to the trace provider then.
trace_provider_t* trace_provider_create(async_dispatcher_t* dispatcher);

// Destroys the trace provider.
void trace_provider_destroy(trace_provider_t* provider);

__END_CDECLS

#ifdef __cplusplus
namespace trace {

// Convenience RAII wrapper for creating and destroying a trace provider.
class TraceProvider {
public:
    // Creates a trace provider.
    TraceProvider(async_dispatcher_t* dispatcher)
        : provider_(trace_provider_create(dispatcher)) {}

    // Destroys a trace provider.
    ~TraceProvider() {
        if (provider_)
            trace_provider_destroy(provider_);
    }

    // Returns true if the trace provider was created successfully.
    zx_status_t is_valid() const {
        return provider_ != nullptr;
    }

private:
    trace_provider_t* provider_;
};

} // namespace trace
#endif // __cplusplus
