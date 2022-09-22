// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_GLOBAL_VARIABLES_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_GLOBAL_VARIABLES_H_

#include <zircon/compiler.h>

#include <efi/types.h>

__BEGIN_CDECLS

#define EFI_GLOBAL_VARIABLE                                                        \
  {                                                                                \
    0x8BE4DF61, 0x93CA, 0x11d2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } \
  }

extern const efi_guid GlobalVariableGuid;

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_GLOBAL_VARIABLES_H_
