// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/io/c/fidl.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
typedef struct {
    fuchsia_io_NodeOnOpenEvent primary;
    fuchsia_io_NodeInfo extra;
} zxfidl_on_open_t;

__END_CDECLS
