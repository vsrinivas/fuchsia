// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A helper library for connecting to the trace manager via fdio.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FDIO_CONNECT_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FDIO_CONNECT_H_

#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t trace_provider_connect_with_fdio(zx_handle_t* out_client);

// *** PT-127 ****************************************************************
// This function is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

zx_status_t tracelink_provider_connect_with_fdio(zx_handle_t* out_client);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_FDIO_CONNECT_H_
