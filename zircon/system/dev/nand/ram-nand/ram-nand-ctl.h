// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t ram_nand_driver_bind(void* ctx, zx_device_t* parent);

__END_CDECLS
