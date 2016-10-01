// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/types.h>
#include <efi/system-table.h>

typedef struct {
    uint8_t* zeropage;
    uint8_t* cmdline;
    void* image;
    uint32_t pages;
} kernel_t;

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz, void* ramdisk, size_t rsz,
                void* cmdline, size_t csz, void* cmdline2, size_t csz2);
