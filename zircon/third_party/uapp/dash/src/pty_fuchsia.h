// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// This function wraps fuchsia.hardware.pty.Device/ReadEvents.
//
// We do not have a C-friendly interface for reading these events yet, so we wrap this tiny C++
// function.
zx_status_t pty_read_events(zx_handle_t handle, uint32_t* out_events);

__END_CDECLS
