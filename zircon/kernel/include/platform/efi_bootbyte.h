// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_BOOTBYTE_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_BOOTBYTE_H_
#include <stdint.h>

void efi_bootbyte_set_reason(uint64_t reason);

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_BOOTBYTE_H_
