// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform/efi.h>
#include <platform/efi_bootbyte.h>

// Default value for the EFI bootbyte.
#define EFI_BOOT_NORMAL 0x1u

/* EFI Variable for Reboot Reason */
#define ZIRCON_BOOTBYTE_EFIVAR \
  { 'b', 'o', 'o', 't', 'b', 'y', 't', 'e', '\0' }

#define ZIRCON_BOOTBYTE_EFIATTR \
  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

namespace {
efi_guid kZirconVendorGuid = ZIRCON_VENDOR_GUID;
char16_t kZirconBootbyteEfiVarName[] = ZIRCON_BOOTBYTE_EFIVAR;
}  // namespace

NO_ASAN void efi_bootbyte_set_reason(uint64_t reason) {
  // Switch into the EFI address space.
  EfiServicesActivation services = TryActivateEfiServices();
  if (!services.valid()) {
    return;
  }

  // Set boot reason, clamping to be in the range of uint8_t.
  uint8_t val;
  if (reason <= 255) {
    val = (uint8_t)reason;
  } else {
    val = EFI_BOOT_NORMAL;
  }
  efi_status status = services->SetVariable(kZirconBootbyteEfiVarName, &kZirconVendorGuid,
                                            ZIRCON_BOOTBYTE_EFIATTR, sizeof(val), &val);

  if (status != EFI_SUCCESS) {
    printf("EFI error while attempting to store bootbyte: %" PRIx64 "\n", status);
  }
}
