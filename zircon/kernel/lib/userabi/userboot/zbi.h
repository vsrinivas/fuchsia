// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_ZBI_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_ZBI_H_

#include <lib/zx/debuglog.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "option.h"

zx::vmo GetBootfsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self,
                         const zx::vmo& zbi_vmo);

Options GetOptionsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self, const zx::vmo& zbi);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_ZBI_H_
