// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <printf.h>
#include <stdio.h>
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

// Super basic single-character UTF-16 to ASCII conversion. Anything outside of
// the [0x00, 0x7F] range just gets converted to '\0'.
static char simple_utf16_to_ascii(uint16_t utf16) {
  if ((utf16 & 0xFF80) == 0) {
    return (char)(utf16 & 0x7F);
  }
  return '\0';
}

// Inner loop for xefi_getc().
static int xefi_getc_loop(int64_t timeout_ms) {
  int result = -1;
  efi_status status;
  efi_event timer_event;

  if (timeout_ms > 0) {
    status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer_event);
    if (status != EFI_SUCCESS) {
      printf("%s: failed to create timer event: %s\n", __func__, xefi_strerror(status));
      return -1;
    }
    // SetTimer() is in 100ns time units.
    status = gBS->SetTimer(timer_event, TimerRelative, timeout_ms * 10000);
    if (status != EFI_SUCCESS) {
      printf("%s: failed to set timer: %s\n", __func__, xefi_strerror(status));
      gBS->CloseEvent(timer_event);
      return -1;
    }
  }

  // Run the checks at least once so we poll if timeout == 0.
  do {
    // Console input gets priority, check it first.
    efi_input_key key;
    if (gSys->ConIn->ReadKeyStroke(gSys->ConIn, &key) == EFI_SUCCESS && key.UnicodeChar != 0) {
      result = simple_utf16_to_ascii(key.UnicodeChar);
      break;
    }

    if (gSerial) {
      char read_char;
      uint64_t read_len = 1;
      status = gSerial->Read(gSerial, &read_len, &read_char);
      if (status == EFI_SUCCESS && read_len == 1) {
        result = read_char;
        break;
      }
    }
  } while ((timeout_ms < 0) || (timeout_ms > 0 && gBS->CheckEvent(timer_event) == EFI_NOT_READY));

  if (timeout_ms > 0) {
    gBS->CloseEvent(timer_event);
  }

  return result;
}

int xefi_getc(int64_t timeout_ms) {
  serial_io_mode mode;
  if (gSerial) {
    // Serial I/O protocol doesn't seem to have any support for on-key events,
    // so we need to poll. The default timeout is 1 second, drop it down so we
    // can alternate checking console and serial.
    mode = *gSerial->Mode;
    efi_status status = gSerial->SetAttributes(gSerial, mode.BaudRate, mode.ReceiveFifoDepth,
                                               1000 /* 1000us = 1ms */, mode.Parity,
                                               (uint8_t)mode.DataBits, mode.StopBits);
    if (status != EFI_SUCCESS) {
      printf("%s: failed to set serial timeout: %s\n", __func__, xefi_strerror(status));
      return -1;
    }
  }

  int result = xefi_getc_loop(timeout_ms);

  if (gSerial) {
    // Restore serial attributes to the previous values.
    efi_status status =
        gSerial->SetAttributes(gSerial, mode.BaudRate, mode.ReceiveFifoDepth, mode.Timeout,
                               mode.Parity, (uint8_t)mode.DataBits, mode.StopBits);
    if (status != EFI_SUCCESS) {
      // Even though we already completed the function at this point, return
      // an error code because the serial might be broken from now on.
      printf("%s: failed to restore serial attributes: %s\n", __func__, xefi_strerror(status));
      return -1;
    }
  }

  return result;
}

void xefi_fatal(const char* msg, efi_status status) {
  printf("\nERROR: %s (%s)\n", msg, xefi_strerror(status));
  xefi_getc(-1);
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
    ERR_ENTRY(EFI_CONNECTION_FIN);
    ERR_ENTRY(EFI_CONNECTION_RESET);
    ERR_ENTRY(EFI_CONNECTION_REFUSED);
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
    ERR_ENTRY(EFI_CONNECTION_FIN);
    ERR_ENTRY(EFI_CONNECTION_RESET);
    ERR_ENTRY(EFI_CONNECTION_REFUSED);
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
