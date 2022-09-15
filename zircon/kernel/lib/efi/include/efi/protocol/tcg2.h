// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Types are defined according to "TCG EFI Protocol Specification"

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCG2_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCG2_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <efi/types.h>

__BEGIN_CDECLS

#define EFI_TCG2_PROTOCOL_GUID                                                     \
  {                                                                                \
    0x607f766c, 0x7455, 0x42be, { 0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f } \
  }

extern const efi_guid Tcg2Protocol;

/* values for EFI_TCG2_EVENT_LOG_FORMAT */
#define EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2 0x00000001
#define EFI_TCG2_EVENT_LOG_FORMAT_TCG_2 0x00000002

/* values for EFI_TCG2_EVENT_ALGORITHM_BITMAP */
#define EFI_TCG2_BOOT_HASH_ALG_SHA1 0x00000001
#define EFI_TCG2_BOOT_HASH_ALG_SHA256 0x00000002
#define EFI_TCG2_BOOT_HASH_ALG_SHA384 0x00000004
#define EFI_TCG2_BOOT_HASH_ALG_SHA512 0x00000008
#define EFI_TCG2_BOOT_HASH_ALG_SM3_256 0x00000010

typedef struct {
  uint8_t Major;
  uint8_t Minor;
} __attribute__((packed)) efi_tcg2_version;

typedef struct {
  uint8_t size;
  efi_tcg2_version StructureVersion;
  efi_tcg2_version ProtocolVersion;
  uint32_t HashAlgorithmBitmap;
  uint32_t SupportedEventLogs;
  uint8_t TPMPresentFlag;
  uint16_t MaxCommandSize;
  uint16_t MaxResponseSize;
  uint32_t ManufacturerID;
  uint32_t NumberOfPcrBanks;
  uint32_t ActivePcrBanks;
} efi_tcg2_boot_service_capability;  // not packed ("TCG EFI Protocol Specification 6.4")
_Static_assert(sizeof(efi_tcg2_boot_service_capability) == 36,
               "Wrong efi_tcg2_boot_service_capability size");

typedef struct efi_tcg2_event_header {
  uint32_t HeaderSize;
  uint16_t HeaderVersion;
  uint32_t PCRIndex;
  uint32_t EventType;
} __attribute__((packed)) efi_tcg2_event_header;

typedef struct efi_tcg2_event {
  uint32_t Size;
  efi_tcg2_event_header Header;
  uint8_t Event[];
} __attribute__((packed)) efi_tcg2_event;

typedef struct efi_tcg2_protocol {
  efi_status (*GetCapability)(struct efi_tcg2_protocol*, efi_tcg2_boot_service_capability*) EFIAPI;
  efi_status (*GetEventLog)(struct efi_tcg2_protocol*, uint32_t event_log_format,
                            void** event_log_location, void** event_log_last_entry,
                            bool* event_log_truncated) EFIAPI;
  efi_status (*HashLogExtendEvent)(struct efi_tcg2_protocol*, uint64_t flags, void* data_to_hash,
                                   uint64_t data_len, efi_tcg2_event* tcg_event) EFIAPI;
  efi_status (*SubmitCommand)(struct efi_tcg2_protocol*, uint32_t block_size, uint8_t* block_data,
                              uint32_t output_size, uint8_t* output_data) EFIAPI;
  efi_status (*GetActivePcrBanks)(struct efi_tcg2_protocol*, uint32_t* active_pcr_banks) EFIAPI;
  efi_status (*SetActivePcrBanks)(struct efi_tcg2_protocol*, uint32_t active_pcr_banks) EFIAPI;
  efi_status (*GetResultOfSetActivePcrBanks)(struct efi_tcg2_protocol*, uint32_t* present,
                                             uint32_t* response) EFIAPI;
} __attribute__((packed)) efi_tcg2_protocol;

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_TCG2_H_
