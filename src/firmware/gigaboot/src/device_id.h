// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_DEVICE_ID_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_DEVICE_ID_H_

#include <inet6.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

#define DEVICE_ID_MAX 24

void device_id(mac_addr addr, char out[DEVICE_ID_MAX], uint32_t generation);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_DEVICE_ID_H_
