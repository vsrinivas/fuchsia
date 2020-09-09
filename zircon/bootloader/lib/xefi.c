// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <printf.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>

#include <efi/protocol/device-path-to-text.h>
#include <efi/protocol/file.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/types.h>

xefi_global xefi_global_state;

void xefi_init(efi_handle img, efi_system_table* sys) {
  gSys = sys;
  gImg = img;
  gBS = sys->BootServices;
  gConOut = sys->ConOut;

  // TODO: re-evaluate the following when we come across the case of a system
  // with multiple implementations of the serial I/O protocol; we will need a
  // way to choose which one to read from and write to.
  gSerial = NULL;
  efi_status status = gBS->LocateProtocol(&SerialIoProtocol, NULL, (void**)&gSerial);
  if (status) {
    printf("xefi_init: failed to open SerialIoProtocol (%s)\n", xefi_strerror(status));
  }
}

void xefi_wait_any_key(void) {
  efi_simple_text_input_protocol* sii = gSys->ConIn;
  efi_input_key key;
  while (sii->ReadKeyStroke(sii, &key) != EFI_SUCCESS)
    ;
}

void xefi_fatal(const char* msg, efi_status status) {
  printf("\nERROR: %s (%s)\n", msg, xefi_strerror(status));
  xefi_wait_any_key();
  gBS->Exit(gImg, 1, 0, NULL);
}

char16_t* xefi_devpath_to_str(efi_device_path_protocol* path) {
  efi_device_path_to_text_protocol* prot;
  efi_status status = gBS->LocateProtocol(&DevicePathToTextProtocol, NULL, (void**)&prot);
  if (EFI_ERROR(status)) {
    return NULL;
  }
  return prot->ConvertDevicePathToText(path, false, false);
}

int xefi_cmp_guid(efi_guid* guid1, efi_guid* guid2) {
  return memcmp(guid1, guid2, sizeof(efi_guid));
}

char16_t* xefi_handle_to_str(efi_handle h) {
  efi_device_path_protocol* path;
  efi_status status = gBS->HandleProtocol(h, &DevicePathProtocol, (void*)&path);
  if (EFI_ERROR(status)) {
    char16_t* err;
    status = gBS->AllocatePool(EfiLoaderData, sizeof(L"<NoPath>"), (void**)&err);
    if (EFI_ERROR(status)) {
      return NULL;
    }
    gBS->CopyMem(err, L"<NoPath>", sizeof(L"<NoPath>"));
    return err;
  }
  char16_t* str = xefi_devpath_to_str(path);
  if (str == NULL) {
    char16_t* err;
    status = gBS->AllocatePool(EfiLoaderData, sizeof(L"<NoString>"), (void**)&err);
    if (EFI_ERROR(status)) {
      return NULL;
    }
    gBS->CopyMem(err, L"<NoString>", sizeof(L"<NoString>"));
    return err;
  }
  return str;
}

efi_status xefi_open_protocol(efi_handle h, efi_guid* guid, void** ifc) {
  return gBS->OpenProtocol(h, guid, ifc, gImg, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
}

efi_status xefi_close_protocol(efi_handle h, efi_guid* guid) {
  return gBS->CloseProtocol(h, guid, gImg, NULL);
}

const char* xefi_strerror(efi_status status) {
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
#undef ERR_ENTRY
  }

  return "<Unknown error>";
}

const char16_t* xefi_wstrerror(efi_status status) {
  switch (status) {
#define ERR_ENTRY(x) \
  case x: {          \
    return L"" #x;   \
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
#undef ERR_ENTRY
  }

  return L"<Unknown error>";
}

size_t strlen_16(char16_t* str) {
  size_t len = 0;
  while (*(str + len) != '\0') {
    len++;
  }

  return len;
}
