// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <lib/zbi/zbi.h>
#include <stdio.h>

#include <efi/protocol/global-variable.h>
#include <efi/types.h>

namespace gigaboot {

const char* EfiStatusToString(efi_status status) {
  switch (status) {
#define ERR_ENTRY(x) \
  case x: {          \
    return #x;       \
  }
    ERR_ENTRY(EFI_SUCCESS);
    ERR_ENTRY(EFI_LOAD_ERROR);
    ERR_ENTRY(EFI_INVALID_PARAMETER);
    ERR_ENTRY(EFI_UNSUPPORTED);
    ERR_ENTRY(EFI_BAD_BUFFER_SIZE);
    ERR_ENTRY(EFI_BUFFER_TOO_SMALL);
    ERR_ENTRY(EFI_NOT_READY);
    ERR_ENTRY(EFI_DEVICE_ERROR);
    ERR_ENTRY(EFI_WRITE_PROTECTED);
    ERR_ENTRY(EFI_OUT_OF_RESOURCES);
    ERR_ENTRY(EFI_VOLUME_CORRUPTED);
    ERR_ENTRY(EFI_VOLUME_FULL);
    ERR_ENTRY(EFI_NO_MEDIA);
    ERR_ENTRY(EFI_MEDIA_CHANGED);
    ERR_ENTRY(EFI_NOT_FOUND);
    ERR_ENTRY(EFI_ACCESS_DENIED);
    ERR_ENTRY(EFI_NO_RESPONSE);
    ERR_ENTRY(EFI_NO_MAPPING);
    ERR_ENTRY(EFI_TIMEOUT);
    ERR_ENTRY(EFI_NOT_STARTED);
    ERR_ENTRY(EFI_ALREADY_STARTED);
    ERR_ENTRY(EFI_ABORTED);
    ERR_ENTRY(EFI_ICMP_ERROR);
    ERR_ENTRY(EFI_TFTP_ERROR);
    ERR_ENTRY(EFI_PROTOCOL_ERROR);
    ERR_ENTRY(EFI_INCOMPATIBLE_VERSION);
    ERR_ENTRY(EFI_SECURITY_VIOLATION);
    ERR_ENTRY(EFI_CRC_ERROR);
    ERR_ENTRY(EFI_END_OF_MEDIA);
    ERR_ENTRY(EFI_END_OF_FILE);
    ERR_ENTRY(EFI_INVALID_LANGUAGE);
    ERR_ENTRY(EFI_COMPROMISED_DATA);
    ERR_ENTRY(EFI_IP_ADDRESS_CONFLICT);
    ERR_ENTRY(EFI_HTTP_ERROR);
    ERR_ENTRY(EFI_CONNECTION_FIN);
    ERR_ENTRY(EFI_CONNECTION_RESET);
    ERR_ENTRY(EFI_CONNECTION_REFUSED);
#undef ERR_ENTRY
  }

  return "<Unknown error>";
}

// Converts an EFI memory type to a zbi_mem_range_t type.
uint32_t EfiToZbiMemRangeType(uint32_t efi_mem_type) {
  switch (efi_mem_type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
      return ZBI_MEM_RANGE_RAM;
  }
  return ZBI_MEM_RANGE_RESERVED;
}

uint64_t ToBigEndian(uint64_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(val);
#else
  return val;
#endif
}

uint64_t BigToHostEndian(uint64_t val) { return ToBigEndian(val); }

efi_status PrintTpm2Capability() {
  auto tpm2_protocol = gigaboot::EfiLocateProtocol<efi_tcg2_protocol>();
  if (tpm2_protocol.is_error()) {
    return tpm2_protocol.error_value();
  }

  printf("Found TPM 2.0 EFI protocol.\n");

  // Log TPM capability
  efi_tcg2_boot_service_capability capability;
  efi_status status = tpm2_protocol->GetCapability(tpm2_protocol.value().get(), &capability);
  if (status != EFI_SUCCESS) {
    return status;
  }

  printf("TPM 2.0 Capabilities:\n");

#define PRINT_NAMED_VAL(field, format) printf(#field " = " format "\n", (field))

  // Structure version
  PRINT_NAMED_VAL(capability.StructureVersion.Major, "0x%02x");
  PRINT_NAMED_VAL(capability.StructureVersion.Minor, "0x%02x");

  // Protocol version
  PRINT_NAMED_VAL(capability.ProtocolVersion.Major, "0x%02x");
  PRINT_NAMED_VAL(capability.ProtocolVersion.Minor, "0x%02x");

#define PRINT_NAMED_BIT(flags, bit) printf(#flags "." #bit "= %d\n", ((flags) & (bit)) ? 1 : 0)

  // Supported hash algorithms
  PRINT_NAMED_BIT(capability.HashAlgorithmBitmap, EFI_TCG2_BOOT_HASH_ALG_SHA1);
  PRINT_NAMED_BIT(capability.HashAlgorithmBitmap, EFI_TCG2_BOOT_HASH_ALG_SHA256);
  PRINT_NAMED_BIT(capability.HashAlgorithmBitmap, EFI_TCG2_BOOT_HASH_ALG_SHA384);
  PRINT_NAMED_BIT(capability.HashAlgorithmBitmap, EFI_TCG2_BOOT_HASH_ALG_SHA512);
  PRINT_NAMED_BIT(capability.HashAlgorithmBitmap, EFI_TCG2_BOOT_HASH_ALG_SM3_256);

  // Supported event logs
  PRINT_NAMED_BIT(capability.SupportedEventLogs, EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2);
  PRINT_NAMED_BIT(capability.SupportedEventLogs, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2);

  // Others
  PRINT_NAMED_VAL(capability.ProtocolVersion.Minor, "0x%02x");
  PRINT_NAMED_VAL(capability.TPMPresentFlag, "0x%02x");
  PRINT_NAMED_VAL(capability.MaxCommandSize, "0x%04x");
  PRINT_NAMED_VAL(capability.MaxResponseSize, "0x%04x");
  PRINT_NAMED_VAL(capability.ManufacturerID, "0x%08x");
  PRINT_NAMED_VAL(capability.NumberOfPcrBanks, "0x%08x");
  PRINT_NAMED_VAL(capability.ActivePcrBanks, "0x%08x");

#undef PRINT_NAMED_VAL
#undef PRINT_NAMED_BIT

  return EFI_SUCCESS;
}

fit::result<efi_status, bool> IsSecureBootOn() {
  size_t size = 1;
  uint8_t value;
  char16_t name[] = u"SecureBoot";
  efi_guid global_var_guid = GlobalVariableGuid;
  efi_status status =
      gEfiSystemTable->RuntimeServices->GetVariable(name, &global_var_guid, NULL, &size, &value);
  if (status != EFI_SUCCESS) {
    return fit::error(status);
  }

  return fit::ok(value);
}

}  // namespace gigaboot
