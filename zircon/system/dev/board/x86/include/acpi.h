// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/platform/bus.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

zx_status_t acpi_init(void);
zx_status_t publish_acpi_devices(zx_device_t* parent, zx_device_t* sys_root,
                                 zx_device_t* acpi_root);
zx_status_t acpi_suspend(uint32_t flags);

__END_CDECLS
