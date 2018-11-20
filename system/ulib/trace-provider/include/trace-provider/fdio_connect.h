// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A helper library for connecting to the trace manager via fdio.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t trace_provider_connect_with_fdio(zx_handle_t* out_client);

__END_CDECLS
