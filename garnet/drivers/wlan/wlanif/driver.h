// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/compiler.h>

// Callbacks for wlanif_driver_ops
__BEGIN_CDECLS
zx_status_t wlanif_bind(void* ctx, zx_device_t* device);
__END_CDECLS
