// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// This value is passed to bti_create as a marker; it does not have a particular
// meaning to anything in the system.
#define SYSMEM_BTI_ID 0x1234123412341234ULL

// Publish a pbus device under sysroot, with access to the given BTI handle.
// Unconditionally takes ownership of the BTI handle.
zx_status_t publish_sysmem(zx_handle_t bti, zx_device_t* sys_root);

__END_CDECLS
