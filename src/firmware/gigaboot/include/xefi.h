// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_XEFI_H_
#define ZIRCON_BOOTLOADER_INCLUDE_XEFI_H_

#include <efi/boot-services.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/file.h>
#include <efi/protocol/serial-io.h>
#include <efi/protocol/simple-text-output.h>
#include <efi/system-table.h>
#include <efi/types.h>

void xefi_init(efi_handle img, efi_system_table* sys);

void xefi_wait_any_key(void);
void xefi_fatal(const char* msg, efi_status status);
char16_t* xefi_handle_to_str(efi_handle handle);
const char* xefi_strerror(efi_status status);
const char16_t* xefi_wstrerror(efi_status status);
size_t strlen_16(char16_t* str);

char16_t* xefi_devpath_to_str(efi_device_path_protocol* path);

int xefi_cmp_guid(efi_guid* guid1, efi_guid* guid2);

// Convenience wrappers for Open/Close protocol for use by
// UEFI app code that's not a driver model participant
efi_status xefi_open_protocol(efi_handle h, efi_guid* guid, void** ifc);
efi_status xefi_close_protocol(efi_handle h, efi_guid* guid);

efi_file_protocol* xefi_open_file(const char16_t* filename);
void* xefi_read_file(efi_file_protocol* file, size_t* _sz, size_t front_bytes);
void* xefi_load_file(const char16_t* filename, size_t* size_out, size_t front_bytes);

efi_status xefi_find_pci_mmio(efi_boot_services* bs, uint8_t cls, uint8_t sub, uint8_t ifc,
                              uint64_t* mmio);

efi_status xefi_get_load_options(size_t* load_options_size, void** load_options);

// GUIDs
extern efi_guid SimpleFileSystemProtocol;
extern efi_guid FileInfoGUID;

typedef struct {
  efi_handle img;
  efi_system_table* sys;
  efi_boot_services* bs;
  efi_simple_text_output_protocol* conout;
  efi_serial_io_protocol* serial;
} xefi_global;

extern xefi_global xefi_global_state;

// Global Context
#define gImg (xefi_global_state.img)
#define gSys (xefi_global_state.sys)
#define gBS (xefi_global_state.bs)
#define gConOut (xefi_global_state.conout)
#define gSerial (xefi_global_state.serial)

#endif  // ZIRCON_BOOTLOADER_INCLUDE_XEFI_H_
