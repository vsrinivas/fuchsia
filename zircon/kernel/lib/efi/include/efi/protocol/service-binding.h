// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERVICE_BINDING_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERVICE_BINDING_H_

#include <zircon/compiler.h>

#include <efi/types.h>

__BEGIN_CDECLS

typedef struct efi_service_binding_protocol {
  efi_status (*CreateChild)(struct efi_service_binding_protocol* self,
                            efi_handle* child_handle) EFIAPI;

  efi_status (*DestroyChild)(struct efi_service_binding_protocol* self,
                             efi_handle child_handle) EFIAPI;
} efi_service_binding_protocol;

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERVICE_BINDING_H_
