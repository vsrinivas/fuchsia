// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_DEVICE_PATH_TO_TEXT_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_DEVICE_PATH_TO_TEXT_H_

#include <stdbool.h>

#include <efi/protocol/device-path.h>
#include <efi/types.h>

#define EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID                                      \
  {                                                                                \
    0x8b843e20, 0x8132, 0x4852, { 0x90, 0xcc, 0x55, 0x1a, 0x4e, 0x4a, 0x7f, 0x1c } \
  }
extern efi_guid DevicePathToTextProtocol;

typedef struct efi_device_path_to_text_protocol {
  char16_t* (*ConvertDeviceNodeToText)(const efi_device_path_protocol* dev_node, bool display_only,
                                       bool allow_shortcuts)EFIAPI;

  char16_t* (*ConvertDevicePathToText)(const efi_device_path_protocol* dev_path, bool display_only,
                                       bool allow_shortcuts)EFIAPI;
} efi_device_path_to_text_protocol;

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_DEVICE_PATH_TO_TEXT_H_
