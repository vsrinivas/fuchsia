// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_SYSMEM_H_
#define SYSROOT_ZIRCON_DEVICE_SYSMEM_H_

#include <ddk/metadata.h>

// "SyM"
#define SYSMEM_METADATA (0x53794d00 | DEVICE_METADATA_PRIVATE)

typedef struct {
    uint32_t vid;
    uint32_t pid;
    uint64_t protected_memory_size;
} sysmem_metadata_t;

#endif  // SYSROOT_ZIRCON_DEVICE_SYSMEM_H_
