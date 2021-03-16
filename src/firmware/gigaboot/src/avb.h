// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_AVB_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_AVB_H_

#include <zircon/compiler.h>

#include <efi/boot-services.h>

__BEGIN_CDECLS

void append_avb_zbi_items(efi_handle img, efi_system_table* sys, void* zbi, size_t zbi_size,
                          const char* ab_suffix);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_AVB_H_
