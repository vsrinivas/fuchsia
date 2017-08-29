// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The API for initializing the trace provider for a process.
//

#pragma once

#include <magenta/compiler.h>

#include <async/dispatcher.h>

__BEGIN_CDECLS

// Represents a trace provider.
typedef struct trace_provider trace_provider_t;

// Creates a trace provider associated with the specified async dispatcher
// and registers it with the tracing system.
//
// The trace provider will start and stop the trace engine in response to requests
// from the tracing system.
//
// |async| is the asynchronous dispatcher which the trace provider and trace
// engine will use for dispatch.  This must outlive the trace provider instance.
//
// Returns the trace provider, or null if creation failed.
//
// TODO(MG-1036): Currently this connects to the trace manager service.
// Switch to passively exporting the trace provider via the "hub" through
// the process's exported directory once that stuff is implemented.  We'll
// probably need to pass some extra parameters to the trace provider then.
trace_provider_t* trace_provider_create(async_t* async);

// Destroys the trace provider.
void trace_provider_destroy(trace_provider_t* provider);

__END_CDECLS

#ifdef __cplusplus
namespace trace {

// Convenience RAII wrapper for creating and destroying a trace provider.
class TraceProvider {
public:
    // Creates a trace provider.
    TraceProvider(async_t* async)
        : provider_(trace_provider_create(async)) {}

    // Destroys a trace provider.
    ~TraceProvider() {
        if (provider_)
            trace_provider_destroy(provider_);
    }

    // Returns true if the trace provider was created successfully.
    mx_status_t is_valid() const {
        return provider_ != nullptr;
    }

private:
    trace_provider_t* provider_;
};

} // namespace trace
#endif // __cplusplus
