// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_H_

#include <zircon/compiler.h>

#include <ddk/device.h>
#include <ddk/protocol/platform/bus.h>

__BEGIN_CDECLS

zx_status_t publish_acpi_devices(zx_device_t* parent, zx_device_t* sys_root,
                                 zx_device_t* acpi_root);
zx_status_t acpi_suspend(uint32_t flags);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_INCLUDE_ACPI_H_
