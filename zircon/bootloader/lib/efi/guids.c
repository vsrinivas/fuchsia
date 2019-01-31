// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/device-path-to-text.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/driver-binding.h>
#include <efi/protocol/file.h>
#include <efi/protocol/graphics-output.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/managed-network.h>
#include <efi/protocol/pci-root-bridge-io.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/simple-network.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/protocol/simple-text-output.h>
#include <efi/protocol/usb-io.h>

efi_guid BlockIoProtocol = EFI_BLOCK_IO_PROTOCOL_GUID;
efi_guid DevicePathProtocol = EFI_DEVICE_PATH_PROTOCOL_GUID;
efi_guid DevicePathToTextProtocol = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID;
efi_guid DiskIoProtocol = EFI_DISK_IO_PROTOCOL_GUID;
efi_guid DriverBindingProtocol = EFI_DRIVER_BINDING_PROTOCOL_GUID;
efi_guid FileInfoGuid = EFI_FILE_INFO_GUID;
efi_guid FileSystemInfoGuid = EFI_FILE_SYSTEM_INFO_GUID;
efi_guid GraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
efi_guid LoadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
efi_guid ManagedNetworkProtocol = EFI_MANAGED_NETWORK_PROTOCOL_GUID;
efi_guid PciRootBridgeIoProtocol = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;
efi_guid SimpleFileSystemProtocol = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
efi_guid SimpleNetworkProtocol = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
efi_guid SimpleTextInputProtocol = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
efi_guid SimpleTextOutputProtocol = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
efi_guid UsbIoProtocol = EFI_USB_IO_PROTOCOL_GUID;
