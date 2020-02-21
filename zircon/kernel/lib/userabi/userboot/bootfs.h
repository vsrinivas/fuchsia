// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_

#include <zircon/types.h>

#include <cstddef>

struct bootfs {
  zx_handle_t vmo;
  const std::byte* contents;
  size_t len;
};

void bootfs_mount(zx_handle_t vmar, zx_handle_t log, zx_handle_t vmo, struct bootfs* fs);
void bootfs_unmount(zx_handle_t vmar, zx_handle_t log, struct bootfs* fs);

zx_handle_t bootfs_open(zx_handle_t log, const char* purpose, struct bootfs* fs,
                        const char* root_prefix, const char* filename);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
