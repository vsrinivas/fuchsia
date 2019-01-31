// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The API for instrumenting C and C++ programs with trace events.
//
// This header defines macros which are used to record trace information during
// program execution when tracing is enabled.  Each trace event macro records
// an event of a given type together with the current time, a category, name,
// and named arguments containing additional information about the event.
//
// Where indicated, the category and name literal strings must point to
// null-terminated static string constants whose memory address can be
// cached by the string table for the lifetime of the trace session.
//
// Defining the NTRACE macro completely disables recording of trace events
// in the compilation unit.
//
// For more control over how trace events are written, see <trace-engine/context.h>.
//

#ifndef TRACE_EVENT_H_
#define TRACE_EVENT_H_

// For now userspace and DDK tracing share the same API and implementation.
#include <trace/internal/event_common.h>

#endif // TRACE_EVENT_H_
