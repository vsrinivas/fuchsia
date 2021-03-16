// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_CTL_H_
#define SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_CTL_H_

#include <lib/ddk/device.h>
#include <zircon/types.h>

zx_status_t RamNandDriverBind(void* ctx, zx_device_t* parent);

#endif  // SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_CTL_H_
