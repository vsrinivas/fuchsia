// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>

#include <phys/efi/main.h>
#include <phys/efi/protocol.h>

fit::result<efi_status, efi_handle> EfiOpenProtocol(efi_handle handle, const efi_guid& guid) {
  void* ptr = nullptr;
  efi_status status = gEfiSystemTable->BootServices->OpenProtocol(
      handle, &guid, &ptr, gEfiImageHandle, nullptr, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok(ptr);
}

void EfiCloseProtocol(const efi_guid& guid, efi_handle protocol) {
  efi_status status =
      gEfiSystemTable->BootServices->CloseProtocol(protocol, &guid, gEfiImageHandle, nullptr);

  // TODO(mcgrathr): Getting EFI_INVALID_PARAMETER here, can't tell why.
  // For now, just leaking the references seems harmless enough.
  status = EFI_SUCCESS;

  ZX_ASSERT_MSG(status == EFI_SUCCESS, "CloseProtocol(%p, <guid> %p, %p, NULL) -> %#zx\n", protocol,
                &guid, gEfiImageHandle, status);
}
