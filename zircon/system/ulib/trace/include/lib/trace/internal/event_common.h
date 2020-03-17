// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains definitions common to userspace and DDK tracing.

#ifndef LIB_TRACE_INTERNAL_EVENT_COMMON_H_
#define LIB_TRACE_INTERNAL_EVENT_COMMON_H_

#include <lib/trace/event_args.h>
#include <lib/trace/internal/event_internal.h>

// Returns true if tracing is enabled.
//
// Usage:
//
//     if (TRACE_ENABLED()) {
//         // do something possibly expensive only when tracing is enabled
//     }
//
#ifndef NTRACE
#define TRACE_ENABLED() (trace_is_enabled())
#else
#define TRACE_ENABLED() (false)
#endif  // NTRACE

// Returns true if tracing of the specified category has been enabled (which
// implies that |TRACE_ENABLED()| is also true).
//
// |category_literal| must be a null-terminated static string constant.
//
// Usage:
//
//     if (TRACE_CATEGORY_ENABLED("category")) {
//         // do something possibly expensive only when tracing this category
//     }
//
#define TRACE_CATEGORY_ENABLED(category_literal) TRACE_INTERNAL_CATEGORY_ENABLED(category_literal)

// Returns a new unique 64-bit unsigned integer (within this process).
// Each invocation returns a different non-zero value.
// Useful for generating identifiers for async and flow events.
//
// Usage:
//
//     trace_async_id_t async_id = TRACE_NONCE();
//     TRACE_ASYNC_BEGIN("category", "name", async_id);
//     // a little while later...
//     TRACE_ASYNC_END("category", "name", async_id);
//
#define TRACE_NONCE() (trace_generate_nonce())

// Writes an instant event representing a single moment in time (a probe).
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the moment with additional information.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |scope| is |TRACE_SCOPE_THREAD|, |TRACE_SCOPE_PROCESS|, or |TRACE_SCOPE_GLOBAL|.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "x", TA_INT32(42));
//
#define TRACE_INSTANT(category_literal, name_literal, scope, args...) \
  TRACE_INTERNAL_INSTANT((category_literal), (name_literal), (scope), args)

// Writes a counter event with the specified id.
//
// The arguments to this event are numeric samples are typically represented by
// the visualizer as a stacked area chart.  The id serves to distinguish multiple
// instances of counters which share the same category and name within the
// same process.
//
// 1 to 15 numeric arguments can be associated with the event, each of which is
// interpreted as a distinct time series.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |counter_id| is the correlation id of the counter.
//              Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_counter_id_t counter_id = 555;
//     TRACE_COUNTER("category", "name", counter_id, "x", TA_INT32(42), "y", TA_DOUBLE(2.0))
//
#define TRACE_COUNTER(category_literal, name_literal, counter_id, arg1, args...) \
  TRACE_INTERNAL_COUNTER((category_literal), (name_literal), (counter_id), arg1, ##args)

// Writes a duration event which ends when the current scope exits.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     void function(int arg) {
//         TRACE_DURATION("category", "name", "arg", TA_INT32(arg));
//         // do something useful here
//     }
//
#define TRACE_DURATION(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION((category_literal), (name_literal), args)

// Writes a duration begin event only.
// This event must be matched by a duration end event with the same category and name.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments provided
// to matching duration begin and duration end events are combined together in
// the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_DURATION_BEGIN("category", "name", "x", TA_INT32(42));
//
#define TRACE_DURATION_BEGIN(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), args)

// Writes a duration end event only.
//
// Durations describe work which is happening synchronously on one thread.
// They can be nested to represent a control flow stack.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments provided
// to matching duration begin and duration end events are combined together in
// the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     TRACE_DURATION_END("category", "name", "x", TA_INT32(42));
//
#define TRACE_DURATION_END(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION_END((category_literal), (name_literal), args)

// Writes an asynchronous begin event with the specified id.
// This event may be followed by async instant events and must be matched by
// an async end event with the same category, name, and id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_BEGIN("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_BEGIN(category_literal, name_literal, async_id, args...) \
  TRACE_INTERNAL_ASYNC_BEGIN((category_literal), (name_literal), (async_id), args)

// Writes an asynchronous instant event with the specified id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_INSTANT("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_INSTANT(category_literal, name_literal, async_id, args...) \
  TRACE_INTERNAL_ASYNC_INSTANT((category_literal), (name_literal), (async_id), args)

// Writes an asynchronous end event with the specified id.
//
// Asynchronous events describe work which is happening asynchronously and which
// may span multiple threads.  Asynchronous events do not nest.  The id serves
// to correlate the progress of distinct asynchronous operations which share
// the same category and name within the same process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the asynchronous operation with additional information.  The
// arguments provided to matching async begin, async instant, and async end
// events are combined together in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |async_id| is the correlation id of the asynchronous operation.
//            Must be unique for a given process, category, and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_async_id_t async_id = 555;
//     TRACE_ASYNC_END("category", "name", async_id, "x", TA_INT32(42));
//
#define TRACE_ASYNC_END(category_literal, name_literal, async_id, args...) \
  TRACE_INTERNAL_ASYNC_END((category_literal), (name_literal), (async_id), args)

// Writes a flow begin event with the specified id.
// This event may be followed by flow steps events and must be matched by
// a flow end event with the same category, name, and id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t flow_id = 555;
//     TRACE_FLOW_BEGIN("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_BEGIN(category_literal, name_literal, flow_id, args...) \
  TRACE_INTERNAL_FLOW_BEGIN((category_literal), (name_literal), (flow_id), args)

// Writes a flow step event with the specified id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t flow_id = 555;
//     TRACE_FLOW_STEP("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_STEP(category_literal, name_literal, flow_id, args...) \
  TRACE_INTERNAL_FLOW_STEP((category_literal), (name_literal), (flow_id), args)

// Writes a flow end event with the specified id.
//
// Flow events describe control flow handoffs between threads or across processes.
// They are typically represented as arrows in a visualizer.  Flow arrows are
// from the end of the duration event which encloses the beginning of the flow
// to the beginning of the duration event which encloses the next step or the
// end of the flow.  The id serves to correlate flows which share the same
// category and name across processes.
//
// This event must be enclosed in a duration event which represents where
// the flow handoff occurs.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the flow with additional information.  The arguments provided
// to matching flow begin, flow step, and flow end events are combined together
// in the trace; it is not necessary to repeat them.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_flow_id_t id = 555;
//     TRACE_FLOW_END("category", "name", flow_id, "x", TA_INT32(42));
//
#define TRACE_FLOW_END(category_literal, name_literal, flow_id, args...) \
  TRACE_INTERNAL_FLOW_END((category_literal), (name_literal), (flow_id), args)

// Writes a large blob record with the given blob data and metadata.
// Here metadata includes timestamp, thread and process information, and arguments,
// which is what most event records contain.
//
// Blobs which exceed |TRACE_ENCODED_RECORD_MAX_TOTAL_LENGTH| will be silently
// ignored, as will blobs which cannot fit within the remaining space in the
// trace buffer.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |blob| is a pointer to the data.
// |blob_size| is the size, in bytes, of the data.
// |args| is the list of argument key/value pairs.
#define TRACE_BLOB_EVENT(category_literal, name_literal, blob, blob_size, args...) \
  TRACE_INTERNAL_BLOB_EVENT(category_literal, name_literal, blob, blob_size, args)

// Writes a large blob record with the given blob data, with only a
// category and name associated with the blob. This will not contain much
// additional metadata. This means timestamp, thread and process information,
// and arguments are not included with the record.
//
// Blobs which exceed |TRACE_ENCODED_RECORD_MAX_TOTAL_LENGTH| will be silently
// ignored, as will blobs which cannot fit within the remaining space in the
// trace buffer.
//
// |category_literal| and |name_literal| must be null-terminated static string constants.
// |blob| is a pointer to the data.
// |blob_size| is the size, in bytes, of the data.
#define TRACE_BLOB_ATTACHMENT(category_literal, name_literal, blob, blob_size) \
  TRACE_INTERNAL_BLOB_ATTACHMENT(category_literal, name_literal, blob, blob_size)

// Writes a description of a kernel object indicated by |handle|,
// including its koid, name, and the supplied arguments.
//
// 0 to 15 arguments can be associated with the record, each of which is used
// to annotate the handle with additional information.
//
// |handle| is the handle of the object being described.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     zx_handle_t handle = ...;
//     TRACE_KERNEL_OBJECT(handle, "description", TA_STRING("some object"));
//
#define TRACE_KERNEL_OBJECT(handle, args...) TRACE_INTERNAL_KERNEL_OBJECT((handle), args)

// WARNING! |TRACE_BLOB| is deprecated in favor of the |TRACE_BLOB_*| macros.
//
// Writes a blob of binary data to the trace buffer.
//
// |type| is the type of the blob, and must be one of the enums in type
//        |trace_blob_type_t|.
// |name_ref| is the name of the blob, and must be a string literal.
// |blob| is a pointer to the data.
// |blob_size| is the size, in bytes, of the data.
//
// A size of zero is ok. The maximum size of a blob is defined by
// TRACE_MAX_BLOB_SIZE which is slighly less than 32K.
// Exercise caution when emitting blob records: space is shared with all
// trace records and large blobs can eat up space fast.
// The blob must fit in the remaining space in the buffer. If the blob does
// not fit the call silently fails, as do all calls that write trace records
// when the buffer is full.
//
// Usage:
//     size_t blob_size = ...;
//     const void* blob = ...;
//     TRACE_BLOB(TRACE_BLOB_TYPE_DATA, "my-blob", blob, blob_size);
//
#define TRACE_BLOB(type, name, blob, blob_size) \
  TRACE_INTERNAL_BLOB((type), (name), (blob), (blob_size))

// Sends a trigger.
//
// |trigger_name| is the name of the trigger to send.
//
// Trigger names are limited to at most 100 characters.
//
// Usage:
//     TRACE_TRIGGER("my-trigger");
//
#define TRACE_TRIGGER(trigger_name) \
  TRACE_INTERNAL_TRIGGER((trigger_name))

#endif  // LIB_TRACE_INTERNAL_EVENT_COMMON_H_
