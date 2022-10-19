// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Helper functions for setting up and tearing down a test fixture which
// manages the trace engine on behalf of a test.
//

#ifndef TRACE_TEST_UTILS_FIXTURE_H_
#define TRACE_TEST_UTILS_FIXTURE_H_

#include <stddef.h>

#ifdef __cplusplus
#include <lib/trace-engine/buffer_internal.h>

// TODO(dje): Conversion to std string, vector.
#include <fbl/string.h>
#include <fbl/vector.h>
#include <trace-reader/records.h>
#endif

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/trace-engine/types.h>
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

bool fixture_compare_raw_records(const fbl::Vector<trace::Record>& records, size_t start_record,
                                 size_t max_num_records, const char* expected);
bool fixture_compare_n_records(size_t max_num_records, const char* expected,
                               fbl::Vector<trace::Record>* out_records,
                               size_t* out_leading_to_skip);

using trace::internal::trace_buffer_header;
void fixture_snapshot_buffer_header(trace_buffer_header* header);

void fixture_set_up_with_categories(attach_to_thread_t attach_to_thread,
                                    trace_buffering_mode_t mode, size_t buffer_size,
                                    const std::vector<std::string>& categories);

#endif

__BEGIN_CDECLS

void fixture_set_up(attach_to_thread_t attach_to_thread, trace_buffering_mode_t mode,
                    size_t buffer_size);
void fixture_tear_down(void);

void fixture_initialize_engine(void);
void fixture_start_engine(void);
void fixture_stop_engine(void);
void fixture_terminate_engine(void);
void fixture_wait_engine_stopped(void);
bool fixture_wait_alert_notification(void);
bool fixture_compare_last_alert_name(const char* expected_alert_name);

void fixture_shutdown(void);

void fixture_initialize_and_start_tracing(void);

// There are two ways of stopping tracing.
// 1) |fixture_stop_and_terminate_tracing()|:
//    a) stops the engine
//      Equivalent: |fixture_stop_engine()|
//    b) waits for everything to quiesce
//      Equivalent: |fixture_wait_engine_stopped()|
//    c) terminates the engine
//      Equivalent: |fixture_terminate_engine()|
//    d) shuts down the |Fixture| dispatcher loop.
//      Equivalent: |fixture_shutdown()|
//    A variant of this is |fixture_stop_and_terminate_tracing_hard()| which is
//    for specialized cases where the async loop exits forcing the engine to
//    quit on its own. |fixture_stop_and_terminate_tracing_hard()| just does
//    step (d).
//
// 2) Invoke the individual steps separately.
//    Do then when you want control over each step.
void fixture_stop_and_terminate_tracing(void);
void fixture_stop_and_terminate_tracing_hard(void);

async_loop_t* fixture_async_loop(void);
zx_status_t fixture_get_disposition(void);
bool fixture_wait_buffer_full_notification(void);
uint32_t fixture_get_buffer_full_wrapped_count(void);
void fixture_reset_buffer_full_notification(void);
bool fixture_compare_records(const char* expected);

static inline void fixture_scope_cleanup(bool* scope) { fixture_tear_down(); }

__END_CDECLS

#endif  // TRACE_TEST_UTILS_FIXTURE_H_
