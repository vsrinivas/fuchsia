// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_RUST_FACTORY_BOOTFS_UTIL_SRC_FACTORY_BOOTFS_UTIL_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_RUST_FACTORY_BOOTFS_UTIL_SRC_FACTORY_BOOTFS_UTIL_H_

extern "C" {

// Read a bootfs file payload from a zbi image in the given buffer.
// The function is implemented in rust in lib.rs
int get_bootfs_file_payload(const void *zbi, size_t size, const char *file_name, void *payload,
                            size_t *out_size);
}

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_RUST_FACTORY_BOOTFS_UTIL_SRC_FACTORY_BOOTFS_UTIL_H_
