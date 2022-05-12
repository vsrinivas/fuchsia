// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TRACE_PROVIDER_START_H_
#define LIB_TRACE_PROVIDER_START_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// Starts a TraceProvider on a background thread and only returns when the
// TraceProvider's setup is complete.  If tracing is currently enabled,
// that means it only returns when this process's tracing is ready to
// record tracing events.
void trace_provider_start();

__END_CDECLS

#endif  // LIB_TRACE_PROVIDER_START_H_
