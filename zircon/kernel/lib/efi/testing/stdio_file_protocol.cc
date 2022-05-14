// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stdio_file_protocol.h>
#include <sys/stat.h>

#include <cstring>

namespace efi {

efi_status StdioFileProtocol::GetInfo(const efi_guid* info_type, size_t* buf_size, void* buf) {
  if (memcmp(info_type, &FileInfoGuid, sizeof(efi_guid))) {
    return EFI_UNSUPPORTED;
  }

  constexpr size_t kInfoSize = sizeof(efi_file_info) + sizeof(char16_t);

  if (*buf_size < kInfoSize) {
    return EFI_BUFFER_TOO_SMALL;
  }
  *buf_size = kInfoSize;

  efi_file_info* info = static_cast<efi_file_info*>(buf);

  struct stat st;
  if (fstat(fileno(stdio_file_), &st) != 0) {
    return EFI_DEVICE_ERROR;
  }

  *info = {
      .Size = kInfoSize,
      .FileSize = static_cast<uint64_t>(st.st_size),
      .PhysicalSize = static_cast<uint64_t>(st.st_blocks) * 512,
  };
  info->FileName[0] = {};

  return EFI_SUCCESS;
}

efi_status StdioFileProtocol::SetInfo(const efi_guid* info_type, size_t buf_size, void* buf) {
  if (memcmp(info_type, &FileInfoGuid, sizeof(efi_guid))) {
    return EFI_UNSUPPORTED;
  }

  if (buf_size < sizeof(efi_file_info)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  const efi_file_info* info = reinterpret_cast<const efi_file_info*>(buf);
  if (ftruncate(fileno(stdio_file_), static_cast<off_t>(info->FileSize)) < 0) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

}  // namespace efi
