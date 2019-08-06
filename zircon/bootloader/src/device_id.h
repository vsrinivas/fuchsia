// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_DEVICE_ID_H_
#define ZIRCON_BOOTLOADER_SRC_DEVICE_ID_H_

#include <inet6.h>

#define DEVICE_ID_MAX 24

void device_id(mac_addr addr, char out[DEVICE_ID_MAX]);

#endif  // ZIRCON_BOOTLOADER_SRC_DEVICE_ID_H_
