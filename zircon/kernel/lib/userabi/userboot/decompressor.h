// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_DECOMPRESSOR_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_DECOMPRESSOR_H_

#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/types.h>

zx_status_t zbi_decompress(zx_handle_t log, const zx::vmar& vmar, const zx::vmo& vmo,
                           uint64_t vmo_offset, size_t size, const zx::vmo& output,
                           uint64_t output_offset, size_t output_size);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_DECOMPRESSOR_H_
