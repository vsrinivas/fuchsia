// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Helper functions for setting up and tearing down a test fixture which
// manages the trace engine on behalf of a test.
//

#pragma once

#include <stddef.h>

#ifdef __cplusplus
// TODO(dje): Conversion to std string, vector.
#include <fbl/string.h>
#include <fbl/vector.h>
#include <trace-engine/buffer_internal.h>
#include <trace-reader/records.h>
#endif

#include <lib/async-loop/loop.h>
#include <trace-engine/types.h>
#include <zircon/compiler.h>

// Specifies whether the trace engine async loop uses the same thread as the
// app or a different thread.
typedef enum {
    // Use different thread from app.
    kNoAttachToThread,
    // Use same thread as app.
    kAttachToThread,
} attach_to_thread_t;

#ifdef __cplusplus

bool fixture_read_records(fbl::Vector<trace::Record>* out_records);

bool fixture_compare_raw_records(const fbl::Vector<trace::Record>& records,
                                 size_t start_record, size_t max_num_records,
                                 const char* expected);
bool fixture_compare_n_records(size_t max_num_records, const char* expected,
                               fbl::Vector<trace::Record>* out_records,
                               size_t* out_leading_to_skip);

using trace::internal::trace_buffer_header;
void fixture_snapshot_buffer_header(trace_buffer_header* header);

#endif

__BEGIN_CDECLS

void fixture_set_up(attach_to_thread_t attach_to_thread,
                    trace_buffering_mode_t mode, size_t buffer_size);
void fixture_tear_down(void);
void fixture_start_tracing(void);

// There are two ways of stopping tracing.
// 1) |fixture_stop_tracing()|:
//    a) stops the engine,
//    b) waits for everything to quiesce,
//    c) shuts down the dispatcher loop.
//    A variant of this is |fixture_stop_tracing_hard()| which is for
//    specialized cases where the async loop exits forcing the engine to
//    quit on its own.
// 2) |fixture_stop_engine(),fixture_shutdown()|: This variant splits out
//    steps (a) and (c) above, leaving the test free to manage step (b): the
//    quiescence.
void fixture_stop_tracing(void);
void fixture_stop_tracing_hard(void);
void fixture_start_engine(void);
void fixture_stop_engine(void);
void fixture_wait_engine_stopped(void);
void fixture_shutdown(void);

async_loop_t* fixture_async_loop(void);
zx_status_t fixture_get_disposition(void);
bool fixture_wait_buffer_full_notification(void);
uint32_t fixture_get_buffer_full_wrapped_count(void);
void fixture_reset_buffer_full_notification(void);
bool fixture_compare_records(const char* expected);

static inline void fixture_scope_cleanup(bool* scope) {
    fixture_tear_down();
}

__END_CDECLS
