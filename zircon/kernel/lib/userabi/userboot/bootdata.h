// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_

#include <lib/zx/debuglog.h>
#include <zircon/types.h>

zx_handle_t bootdata_get_bootfs(const zx::debuglog& log, zx_handle_t vmar_self, zx_handle_t job,
                                zx_handle_t bootdata_vmo);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_
