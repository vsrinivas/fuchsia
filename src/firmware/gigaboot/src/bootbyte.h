// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_

#include <stdint.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#include <efi/runtime-services.h>

__BEGIN_CDECLS

/* EFI Variable for Reboot Reason */
#define ZIRCON_BOOTBYTE_EFIVAR \
  { 'b', 'o', 'o', 't', 'b', 'y', 't', 'e', '\0' }

#define ZIRCON_BOOTBYTE_EFIATTR \
  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

#define EFI_BOOT_NORMAL 0x1u
#define EFI_BOOT_RECOVERY 0x2u
#define EFI_BOOT_BOOTLOADER 0x4u
#define EFI_BOOT_DEFAULT 0xFFu

efi_status get_bootbyte(efi_runtime_services* runtime, uint8_t* bootbyte);
efi_status set_bootbyte(efi_runtime_services* runtime, uint8_t bootbyte);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_
