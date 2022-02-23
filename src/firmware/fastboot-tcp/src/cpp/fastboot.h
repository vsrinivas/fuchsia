// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_FASTBOOT_H_
#define SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_FASTBOOT_H_

#include <stddef.h>

extern "C" {
// A C style interface for the fastboot rust component to call.
// The function is not thread-safe.
int fastboot_process(size_t packet_size, int (*read_packet_callback)(void *, size_t, void *),
                     int (*write_packet_callback)(const void *, size_t, void *), void *ctx);
}

#endif  // SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_FASTBOOT_H_
