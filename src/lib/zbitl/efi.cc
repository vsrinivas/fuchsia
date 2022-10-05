// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/efi.h>

#include <memory>

namespace zbitl {
namespace {

constexpr size_t kBufferSize = 4096;

union EfiFileInfoBuffer {
  efi_file_info info;
  char space[sizeof(efi_file_info) + sizeof(char16_t[255])];
};

fit::result<efi_status, EfiFileInfoBuffer> EfiFileGetInfo(efi_file_protocol* file) {
  EfiFileInfoBuffer buffer;
  size_t info_size = sizeof(buffer);
  efi_status status = file->GetInfo(file, &FileInfoGuid, &info_size, &buffer);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  ZX_ASSERT(info_size >= sizeof(buffer.info));
  return fit::ok(buffer);
}

}  // namespace

fit::result<efi_status, uint32_t> StorageTraits<efi_file_protocol*>::Capacity(
    efi_file_protocol* file) {
  auto result = EfiFileGetInfo(file);
  if (result.is_error()) {
    return result.take_error();
  }
  const uint64_t size = result->info.FileSize;
  uint64_t capped_size = std::min<uint64_t>(size, std::numeric_limits<uint32_t>::max());
  return fit::ok(static_cast<uint32_t>(capped_size));
}

fit::result<efi_status> StorageTraits<efi_file_protocol*>::EnsureCapacity(efi_file_protocol* file,
                                                                          uint32_t capacity_bytes) {
  auto result = EfiFileGetInfo(file);
  if (result.is_error()) {
    return result.take_error();
  }
  result->info.FileSize = capacity_bytes;
  efi_status status = file->SetInfo(file, &FileInfoGuid, static_cast<size_t>(result->info.Size),
                                    std::addressof(result.value()));
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok();
}

fit::result<efi_status> StorageTraits<efi_file_protocol*>::Read(efi_file_protocol* file,
                                                                uint64_t payload, void* buffer,
                                                                uint32_t length) {
  efi_status status = file->SetPosition(file, payload);
  if (status == EFI_SUCCESS) {
    size_t read_size = length;
    status = file->Read(file, &read_size, buffer);
    if (status == EFI_SUCCESS && read_size != length) {
      status = EFI_END_OF_FILE;
    }
  }
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok();
}

fit::result<efi_status> StorageTraits<efi_file_protocol*>::Write(efi_file_protocol* file,
                                                                 uint32_t offset,
                                                                 zbitl::ByteView data) {
  if (data.empty()) {
    return fit::ok();
  }
  efi_status status = file->SetPosition(file, offset);
  if (status == EFI_SUCCESS) {
    size_t size = data.size();
    status = file->Write(file, &size, data.data());
    if (status == EFI_SUCCESS && size != data.size()) {
      status = EFI_VOLUME_FULL;
    }
  }
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok();
}

fit::result<efi_status> StorageTraits<efi_file_protocol*>::DoRead(efi_file_protocol* file,
                                                                  uint64_t offset, uint32_t length,
                                                                  bool (*cb)(void*, ByteView),
                                                                  void* arg) {
  if (length == 0) {
    cb(arg, {});
    return fit::ok();
  }

  if (efi_status status = file->SetPosition(file, offset); status != EFI_SUCCESS) {
    return fit::error{status};
  }

  auto size = [&]() { return std::min(static_cast<size_t>(length), kBufferSize); };
  std::unique_ptr<std::byte[]> buf{new std::byte[size()]};

  while (length > 0) {
    size_t read_size = size();
    efi_status status = file->Read(file, &read_size, buf.get());
    if (status != EFI_SUCCESS) {
      return fit::error{status};
    }
    ZX_ASSERT(read_size <= kBufferSize);
    if (!cb(arg, {buf.get(), read_size})) {
      break;
    }
    length -= static_cast<uint32_t>(read_size);
  }

  return fit::ok();
}

}  // namespace zbitl
