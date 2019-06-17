// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A helper library for connecting to the trace manager via fdio.

#ifndef ZIRCON_SYSTEM_ULIB_LIB_TRACE_PROVIDER_FDIO_CONNECT_H_
#define ZIRCON_SYSTEM_ULIB_LIB_TRACE_PROVIDER_FDIO_CONNECT_H_

#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t trace_provider_connect_with_fdio(zx_handle_t* out_client);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_ULIB_LIB_TRACE_PROVIDER_FDIO_CONNECT_H_
