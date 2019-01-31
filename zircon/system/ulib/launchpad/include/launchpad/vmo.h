// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <stddef.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// This functions ZX_ERR_IO to indicate an error in the POSIXish
// underlying calls, meaning errno has been set with a POSIX-style error.
// Other errors are verbatim from the zx_vm_object_* calls.
zx_status_t launchpad_vmo_from_file(const char* filename, zx_handle_t* out);

__END_CDECLS
