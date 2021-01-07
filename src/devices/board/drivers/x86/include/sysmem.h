// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <zircon/compiler.h>

#include <ddk/device.h>

zx_status_t publish_sysmem(pbus_protocol_t* pbus);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SYSMEM_H_
