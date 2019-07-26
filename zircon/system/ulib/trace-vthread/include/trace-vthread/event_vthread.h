// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TRACE_<event> macros to simplify emitting vthread events.
// Note: This only provides a C++ implementation.

// A note on the style of the macros here.
// These macros have a public API which is what you see here.
// Underlying that are the *_INTERNAL_* macros which you intentionally do not
// see here. This is to help avoid thinking anything underneath is something
// that is public and usable.

#ifndef TRACE_VTHREAD_EVENT_VTHREAD_H_
#define TRACE_VTHREAD_EVENT_VTHREAD_H_

#include <trace-vthread/internal/event_vthread.h>

// Writes a virtual thread duration begin event.
// This event must be matched by a duration end event with the same category,
// name and virtual thread.
//
// Virtual thread durations describe work which is happening synchronously on
// a timeline other than the CPU's (E.g. the GPU).
// They can be nested to represent a control flow stack.
// The virtual thread id serves to identify the timeline within the process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments
// provided to matching duration begin and duration end events are combined
// together in the trace; it is not necessary to repeat them.
//
// |category_literal|, |name_literal| and |vthread_literal| must be
// null-terminated static string constants.
// |vthread_id| is the correlation id of the virtual thread.
//              Must be unique for a given process.
// |timestamp| is the tick that the duration event begins.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_vthread_id_t vthread_id = 444;
//     zx_ticks_t curr_ticks = zx_ticks_get();
//     TRACE_VTHREAD_DURATION_BEGIN("category", "name", "vthread", vthread_id,
//                                  curr_ticks, "x", TA_INT32(42));
//
#define TRACE_VTHREAD_DURATION_BEGIN(category_literal, name_literal, vthread_literal, vthread_id, \
                                     timestamp, args...)                                          \
  TRACE_VTHREAD_INTERNAL_DURATION_BEGIN((category_literal), (name_literal), (vthread_literal),    \
                                        (vthread_id), (timestamp), args)

// Writes a virtual thread duration end event.
//
// Virtual thread durations describe work which is happening synchronously on
// a timeline other than the CPU's (E.g. the GPU).
// They can be nested to represent a control flow stack.
// The virtual thread id serves to identify the timeline within the process.
//
// 0 to 15 arguments can be associated with the event, each of which is used
// to annotate the duration with additional information.  The arguments
// provided to matching duration begin and duration end events are combined
// together in the trace; it is not necessary to repeat them.
//
// |category_literal|, |name_literal| and |vthread_literal| must be
// null-terminated static string constants.
// |vthread_id| is the correlation id of the virtual thread.
//              Must be unique for a given process.
// |timestamp| is the tick that the duration event ends.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_vthread_id_t vthread_id = 444;
//     zx_ticks_t curr_ticks = zx_ticks_get();
//     TRACE_VTHREAD_DURATION_END("category", "name", "vthread", vthread_id,
//                                curr_ticks, "x", TA_INT32(42));
//
#define TRACE_VTHREAD_DURATION_END(category_literal, name_literal, vthread_literal, vthread_id, \
                                   timestamp, args...)                                          \
  TRACE_VTHREAD_INTERNAL_DURATION_END((category_literal), (name_literal), (vthread_literal),    \
                                      (vthread_id), (timestamp), args)

// Writes a virtual thread flow begin event with the specified id.
// This event may be followed by flow steps events and must be matched by
// a flow end event with the same category, name, virtual thread and id.
//
// Flow events describe control flow handoffs between threads or across
// processes.
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
// to matching flow begin, flow step, and flow end events are combined
// together in the trace; it is not necessary to repeat them.
//
// |category_literal|, |name_literal| and |vthread_literal| must be
// null-terminated static string constants.
// |vthread_id| is the correlation id of the virtual thread.
//              Must be unique for a given process.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |timestamp| is the tick that the flow begins.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_vthread_id_t vthread_id = 444;
//     trace_flow_id_t flow_id = 555;
//     zx_ticks_t curr_ticks = zx_ticks_get();
//     TRACE_VTHREAD_FLOW_BEGIN("category", "name", "vthread", vthread_id,
//                              flow_id, curr_ticks, "x", TA_INT32(42));
//
#define TRACE_VTHREAD_FLOW_BEGIN(category_literal, name_literal, vthread_literal, vthread_id, \
                                 flow_id, timestamp, args...)                                 \
  TRACE_VTHREAD_INTERNAL_FLOW_BEGIN((category_literal), (name_literal), (vthread_literal),    \
                                    (vthread_id), (flow_id), (timestamp), args)

// Writes a virtual thread flow step event with the specified id.
//
// Flow events describe control flow handoffs between threads or across
// processes.
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
// to matching flow begin, flow step, and flow end events are combined
// together in the trace; it is not necessary to repeat them.
//
// |category_literal|, |name_literal| and |vthread_literal| must be
// null-terminated static string constants.
// |vthread_id| is the correlation id of the virtual thread.
//              Must be unique for a given process.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |timestamp| is the tick that the flow steps.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_vthread_id_t vthread_id = 444;
//     trace_flow_id_t flow_id = 555;
//     zx_ticks_t curr_ticks = zx_ticks_get();
//     TRACE_VTHREAD_FLOW_STEP("category", "name", "vthread", vthread_id,
//                             flow_id, curr_ticks, "x", TA_INT32(42));
//
#define TRACE_VTHREAD_FLOW_STEP(category_literal, name_literal, vthread_literal, vthread_id, \
                                flow_id, timestamp, args...)                                 \
  TRACE_VTHREAD_INTERNAL_FLOW_STEP((category_literal), (name_literal), (vthread_literal),    \
                                   (vthread_id), (flow_id), (timestamp), args)

// Writes a virtual thread flow end event with the specified id.
//
// Flow events describe control flow handoffs between threads or across
// processes.
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
// to matching flow begin, flow step, and flow end events are combined
// together in the trace; it is not necessary to repeat them.
//
// |category_literal|, |name_literal| and |vthread_literal| must be
// null-terminated static string constants.
// |vthread_id| is the correlation id of the virtual thread.
//              Must be unique for a given process.
// |flow_id| is the correlation id of the flow.
//           Must be unique for a given category and name combination.
// |timestamp| is the tick that the flow ends.
// |args| is the list of argument key/value pairs.
//
// Usage:
//
//     trace_vthread_id_t vthread_id = 444;
//     trace_flow_id_t flow_id = 555;
//     zx_ticks_t curr_ticks = zx_ticks_get();
//     TRACE_VTHREAD_FLOW_END("category", "name", "vthread", vthread_id,
//                            flow_id, curr_ticks, "x", TA_INT32(42));
//
#define TRACE_VTHREAD_FLOW_END(category_literal, name_literal, vthread_literal, vthread_id, \
                               flow_id, timestamp, args...)                                 \
  TRACE_VTHREAD_INTERNAL_FLOW_END((category_literal), (name_literal), (vthread_literal),    \
                                  (vthread_id), (flow_id), (timestamp), args)

#endif  // TRACE_VTHREAD_EVENT_VTHREAD_H_
