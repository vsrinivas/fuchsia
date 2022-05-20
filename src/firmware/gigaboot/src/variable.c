// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "variable.h"

#include <efi/runtime-services.h>
#include <efi/types.h>

efi_guid kGigabootVendorGuid = {0x6cab6b62, 0xd267, 0x4295, {0x79, 0xce, 0xf6, 0x87, 0x17, 0x50}};

efi_status set_bool(efi_runtime_services* runtime, char16_t* name, bool value) {
  efi_status status = runtime->SetVariable(
      name, &kGigabootVendorGuid, EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
      sizeof(value), &value);

  return status;
}

efi_status get_bool(efi_runtime_services* runtime, char16_t* name, bool* value) {
  size_t size = sizeof(*value);
  efi_status status = runtime->GetVariable(name, &kGigabootVendorGuid, NULL, &size, value);
  if (size != sizeof(*value)) {
    return EFI_BUFFER_TOO_SMALL;
  }
  return status;
}
