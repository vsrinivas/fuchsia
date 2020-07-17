// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_FASTBOOT_H_
#define ZIRCON_BOOTLOADER_SRC_FASTBOOT_H_

#define FB_SERVER_PORT 5554

#include<inttypes.h>

void fb_recv(void *data, size_t len, void *saddr, uint16_t sport, uint16_t dport);

#endif  // ZIRCON_BOOTLOADER_SRC_FASTBOOT_H_
