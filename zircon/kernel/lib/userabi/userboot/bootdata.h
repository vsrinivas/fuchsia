// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_

#include <lib/zx/debuglog.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

zx::vmo bootdata_get_bootfs(const zx::debuglog& log, const zx::vmar& vmar_self,
                            const zx::vmo& bootdata_vmo);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTDATA_H_
