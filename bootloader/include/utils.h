// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <efi/boot-services.h>
#include <efi/system-table.h>
#include <efi/types.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/simple-text-output.h>

void InitGoodies(efi_handle img, efi_system_table* sys);

void WaitAnyKey(void);
void Fatal(const char* msg, efi_status status);
char16_t* HandleToString(efi_handle handle);
const char *efi_strerror(efi_status status);
const char16_t* efi_wstrerror(efi_status status);
size_t strlen_16(char16_t* str);

char16_t* DevicePathToStr(efi_device_path_protocol* path);

int CompareGuid(efi_guid* guid1, efi_guid* guid2);

// Convenience wrappers for Open/Close protocol for use by
// UEFI app code that's not a driver model participant
efi_status OpenProtocol(efi_handle h, efi_guid* guid, void** ifc);
efi_status CloseProtocol(efi_handle h, efi_guid* guid);

void* LoadFile(char16_t* filename, size_t* size_out);

efi_status FindPCIMMIO(efi_boot_services* bs, uint8_t cls, uint8_t sub, uint8_t ifc, uint64_t* mmio);

// GUIDs
extern efi_guid SimpleFileSystemProtocol;
extern efi_guid FileInfoGUID;

// Global Context
extern efi_handle gImg;
extern efi_system_table* gSys;
extern efi_boot_services* gBS;
extern efi_simple_text_output_protocol* gConOut;
