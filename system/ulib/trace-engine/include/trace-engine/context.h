// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The ABI-stable entry points used by trace instrumentation libraries.
//
// These functions are used to write trace records into the trace buffer
// associated with a trace context.
//
// Writing trace records is intended to be very fast but the cost varies
// depending on the size and complexity of the event and any arguments
// which are associated with it.
//
// At this time, there exists only one trace context, the engine's trace context,
// which can be acquired and released using the functions in
// <trace-engine/instrumentation.h>.  In the future, this API may be extended
// to support trace contexts with different scopes.
//
// Client code shouldn't be using these APIs directly.
// See <trace/event.h> for instrumentation macros.
//

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <trace-engine/types.h>

__BEGIN_CDECLS

// Opaque type representing a trace context.
// Most functions in this header require a valid trace context to operate.
typedef struct trace_context trace_context_t;

// Returns true if tracing of the specified category has been enabled.
//
// Use |trace_context_register_category_literal()| if you intend to immediately
// write a record into the trace buffer after checking the category.
//
// |context| must be a valid trace context reference.
// |category_literal| must be a null-terminated static string constant.
//
// This function is thread-safe.
bool trace_context_is_category_enabled(
    trace_context_t* context,
    const char* category_literal);

// Registers a copy of a string into the string table.
//
// Writes a string record into the trace buffer if the string was added to the
// string table.  If the string table is full, returns an inline string reference.
//
// |context| must be a valid trace context reference.
// |string| must be the string to register.
// |length| must be the length of the string.
// |out_ref| points to where the registered string reference should be returned.
//
// This function is thread-safe.
void trace_context_register_string_copy(
    trace_context_t* context,
    const char* string, size_t length,
    trace_string_ref_t* out_ref);

// Registers a copy of a string and returns its string ref.
// Helper for |trace_context_register_thread()|.
inline trace_string_ref_t trace_context_make_registered_string_copy(
    trace_context_t* context,
    const char* string, size_t length) {
    trace_string_ref_t ref;
    trace_context_register_string_copy(context, string, length, &ref);
    return ref;
}

// Registers a string literal into the string table keyed by its address in memory.
//
// The trace context caches the string so that subsequent registrations using
// the same memory address may return the same indexed string reference if
// found in the cache.
//
// Writes a string record into the trace buffer if the string was added to the
// string table.  If the string table is full, returns an inline string reference.
//
// |context| must be a valid trace context reference.
// |string_literal| must be a null-terminated static string constant.
// |out_ref| points to where the registered string reference should be returned.
//
// This function is thread-safe.
void trace_context_register_string_literal(
    trace_context_t* context,
    const char* string_literal,
    trace_string_ref_t* out_ref);

// Registers a string literal and returns its string ref.
// Helper for |trace_context_register_string_literal()|.
inline trace_string_ref_t trace_context_make_registered_string_literal(
    trace_context_t* context,
    const char* string_literal) {
    trace_string_ref_t ref;
    trace_context_register_string_literal(context, string_literal, &ref);
    return ref;
}

// Registers a category into the string table, if it is enabled, keyed by its
// address in memory.
//
// The trace context caches the string so that subsequent registrations using
// the same memory address may return the same indexed string reference if
// found in the cache.
//
// Writes a string record into the trace buffer if the category was added to the
// string table.  If the string table is full, returns an inline string reference.
//
// |context| must be a valid trace context reference.
// |category_literal| must be a null-terminated static string constant.
// |out_ref| points to where the registered string reference should be returned.
//
// Returns true and registers the string if the category is enabled, otherwise
// returns false and does not modify |*out_ref|.
//
// This function is thread-safe.
bool trace_context_register_category_literal(
    trace_context_t* context,
    const char* category_literal,
    trace_string_ref_t* out_ref);

// Registers the current thread into the thread table.
//
// Writes a process and/or thread kernel object record into the trace buffer if
// the process and/or thread have not previously been described.  Writes a
// thread record into the trace buffer if the thread was added to the thread table.
//
// If the thread table is full, returns an inline thread refrence.
//
// |context| must be a valid trace context reference.
// |out_ref| points to where the registered thread reference should be returned.
//
// This function is thread-safe.
void trace_context_register_current_thread(
    trace_context_t* context,
    trace_thread_ref_t* out_ref);

// Registers the specified thread into the thread table.
//
// Writes a thread record into the trace buffer if the thread was added to the
// thread table.
//
// If the thread table is full, returns an inline thread refrence.
//
// Unlike |trace_context_register_current_thread()|, the caller is responsible for
// writing a process and/or thread kernel object record into the trace buffer
// if the process and/or thread have not previously been described.
//
// |context| must be a valid trace context reference.
// |process_koid| is the koid of the process which contains the thread.
// |thread_koid| is the koid of the thread to register.
// |out_ref| points to where the registered thread reference should be returned.
//
// This function is thread-safe.
void trace_context_register_thread(
    trace_context_t* context,
    mx_koid_t process_koid, mx_koid_t thread_koid,
    trace_thread_ref_t* out_ref);

// Registers a thread and returns its thread ref.
// Helper for |trace_context_register_thread()|.
inline trace_thread_ref_t trace_context_make_registered_thread(
    trace_context_t* context,
    mx_koid_t process_koid, mx_koid_t thread_koid) {
    trace_thread_ref_t ref;
    trace_context_register_thread(context, process_koid, thread_koid, &ref);
    return ref;
}

// Writes a kernel object record which describes the specified object into
// the trace buffer.  Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |koid| is the koid of the object being described.
// |type| is the object type.
// |name_ref| is the name of the object.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_kernel_object_record(
    trace_context_t* context,
    mx_koid_t koid, mx_obj_type_t type,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args);

// Writes a kernel object record for the object reference by the specified handle
// into the trace buffer.  Discards the record if it cannot be written.
//
// Collects the necessary information by querying the object's type and properties.
//
// |context| must be a valid trace context reference.
// |handle| is the handle of the object being described.
//
// This function is thread-safe.
void trace_context_write_kernel_object_record_for_handle(
    trace_context_t* context,
    mx_handle_t handle,
    const trace_arg_t* args, size_t num_args);

// Writes a kernel object record for the specified process into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |process_koid| is the koid of the process being described.
// |process_name_ref| is the name of the process.
//
// This function is thread-safe.
void trace_context_write_process_info_record(
    trace_context_t* context,
    mx_koid_t process_koid,
    const trace_string_ref_t* process_name_ref);

// Writes a kernel object record for the specified thread into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |process_koid| is the koid of the process which contains the thread.
// |thread_koid| is the koid of the thread being described.
// |thread_name_ref| is the name of the thread.
//
// This function is thread-safe.
void trace_context_write_thread_info_record(
    trace_context_t* context,
    mx_koid_t process_koid, mx_koid_t thread_koid,
    const trace_string_ref_t* thread_name_ref);

// Writes a context switch record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |cpu_number| is the CPU upon which the context switch occurred.
// |outgoing_thread_state| is the state of the thread which was descheduled from the CPU.
// |outgoing_thread_ref| is the thread which was descheduled from the CPU.
// |incoming_thread_ref| is the thread which was scheduled on the CPU.
//
// This function is thread-safe.
void trace_context_write_context_switch_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    trace_cpu_number_t cpu_number,
    trace_thread_state_t outgoing_thread_state,
    const trace_thread_ref_t* outgoing_thread_ref,
    const trace_thread_ref_t* incoming_thread_ref);

// Writes a log record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread which wrote the log message.
// |log_message| is the content of the log message.
// |log_message_length| is the length of the log message.
//
// This function is thread-safe.
void trace_context_write_log_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const char* log_message,
    size_t log_message_length);

// Writes an instant event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |scope| is the scope to which the instant event applies (thread, process, global).
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_instant_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_scope_t scope,
    const trace_arg_t* args, size_t num_args);

// Writes a counter event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |counter_id| is the correlation id of the counter.
//              Must be unique for a given process, category, and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_counter_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_counter_id_t counter_id,
    const trace_arg_t* args, size_t num_args);

// Writes a duration begin event record and a duration end event record with
// arguments into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_duration_event_record(
    trace_context_t* context,
    trace_ticks_t start_time,
    trace_ticks_t end_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args);

// Writes a duration begin event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_duration_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args);

// Writes a duration end event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_duration_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    const trace_arg_t* args, size_t num_args);

// Writes an asynchronous begin event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_async_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

// Writes an asynchronous instant event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_async_instant_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

// Writes an asynchronous end event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_async_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_async_id_t async_id,
    const trace_arg_t* args, size_t num_args);

// Writes a flow begin event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_flow_begin_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

// Writes a flow step event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_flow_step_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

// Writes a flow end event record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |event_time| is the time of the event, in ticks.
// |thread_ref| is the thread on which the event occurred.
// |category_ref| is the category of the event.
// |name_ref| is the name of the event.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| contains |num_args| key/value pairs to include in the record, or NULL if none.
//
// This function is thread-safe.
void trace_context_write_flow_end_event_record(
    trace_context_t* context,
    trace_ticks_t event_time,
    const trace_thread_ref_t* thread_ref,
    const trace_string_ref_t* category_ref,
    const trace_string_ref_t* name_ref,
    trace_flow_id_t flow_id,
    const trace_arg_t* args, size_t num_args);

// Writes an initialization record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |ticks_per_second| is the number of |trace_ticks_t| per second used in the trace.
//
// This function is thread-safe.
void trace_context_write_initialization_record(
    trace_context_t* context,
    uint64_t ticks_per_second);

// Writes a string record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |index| is the index of the string, between |TRACE_ENCODED_STRING_REF_MIN_INDEX|
//         and |TRACE_ENCODED_STRING_REF_MAX_INDEX| inclusive.
// |string| is the content of the string.
// |length| is the length of the string; the string will be truncated if it is longer
//          than |TRACE_ENCODED_STRING_REF_MAX_LENGTH|.
//
// This function is thread-safe.
void trace_context_write_string_record(
    trace_context_t* context,
    trace_string_index_t index, const char* string, size_t length);

// Writes a thread record into the trace buffer.
// Discards the record if it cannot be written.
//
// |context| must be a valid trace context reference.
// |index| is the index of the thread, between |TRACE_ENCODED_STRING_REF_MIN_INDEX|
//         and |TRACE_ENCODED_THREAD_REF_MAX_INDEX| inclusive.
// |process_koid| is the koid of the process which contains the thread.
// |thread_koid| is the koid of the thread being described.
//
// This function is thread-safe.
void trace_context_write_thread_record(
    trace_context_t* context,
    trace_thread_index_t index,
    mx_koid_t process_koid,
    mx_koid_t thread_koid);

// Allocates space for a record in the trace buffer.
//
// |context| must be a valid trace context reference.
// |num_bytes| must be a multiple of 8 bytes.
//
// Returns a pointer to the allocated space within the trace buffer with
// 8 byte alignment, or NULL if the trace buffer is full or if |num_bytes|
// exceeds |TRACE_ENCODED_RECORD_MAX_LENGTH|.
//
// This function is thread-safe, fail-fast, and lock-free.
void* trace_context_alloc_record(trace_context_t* context, size_t num_bytes);

__END_CDECLS
