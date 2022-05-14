// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STDIO_FILE_PROTOCOL_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STDIO_FILE_PROTOCOL_H_

#include <cstdio>
#include <utility>

#include <efi/protocol/file.h>

#include "mock_protocol_base.h"

namespace efi {

// Wraps efi_file_protocol to support reading/writing a host file via stdio.
class StdioFileProtocol : public MockProtocolBase<StdioFileProtocol, efi_file_protocol> {
 public:
  // The constructor takes ownership of the FILE*.
  StdioFileProtocol(FILE* stdio_file = nullptr)
      : MockProtocolBase<StdioFileProtocol, efi_file_protocol>({
            .Revision = EFI_FILE_PROTOCOL_LATEST_REVISION,
            .Open = Bounce<&StdioFileProtocol::Open>,
            .Close = Bounce<&StdioFileProtocol::Close>,
            .Delete = Bounce<&StdioFileProtocol::Delete>,
            .Read = Bounce<&StdioFileProtocol::Read>,
            .Write = Bounce<&StdioFileProtocol::Write>,
            .GetPosition = Bounce<&StdioFileProtocol::GetPosition>,
            .SetPosition = Bounce<&StdioFileProtocol::SetPosition>,
            .GetInfo = Bounce<&StdioFileProtocol::GetInfo>,
            .SetInfo = Bounce<&StdioFileProtocol::SetInfo>,
            .Flush = Bounce<&StdioFileProtocol::Flush>,
            .OpenEx = Bounce<&StdioFileProtocol::OpenEx>,
            .ReadEx = Bounce<&StdioFileProtocol::ReadEx>,
            .WriteEx = Bounce<&StdioFileProtocol::WriteEx>,
            .FlushEx = Bounce<&StdioFileProtocol::FlushEx>,
        }),
        stdio_file_(stdio_file) {}

  // Move-only.
  StdioFileProtocol(const StdioFileProtocol&) = delete;

  StdioFileProtocol(StdioFileProtocol&& other) noexcept
      : MockProtocolBase<StdioFileProtocol, efi_file_protocol>(other),
        stdio_file_(std::exchange(other.stdio_file_, nullptr)) {}

  StdioFileProtocol& operator=(const StdioFileProtocol&) = delete;

  StdioFileProtocol& operator=(StdioFileProtocol&& other) noexcept {
    stdio_file_ = std::exchange(other.stdio_file_, nullptr);
    return *this;
  }

  ~StdioFileProtocol() override {
    if (stdio_file_) {
      fclose(stdio_file_);
    }
  }

  FILE* stdio_file() { return stdio_file_; }

  static StdioFileProtocol& FromProtocol(efi_file_protocol* file) {
    return *static_cast<Wrapper*>(file)->mock_;
  }

  efi_status Open(struct efi_file_protocol** new_handle, const char16_t* filename,
                  uint64_t open_mode, uint64_t attributes) {
    return EFI_UNSUPPORTED;
  }

  efi_status Close() {
    fclose(std::exchange(stdio_file_, nullptr));
    return EFI_SUCCESS;
  }

  efi_status Delete() { return EFI_UNSUPPORTED; }

  efi_status Read(size_t* len, void* buf) {
    *len = fread(buf, 1, *len, stdio_file_);
    return EFI_SUCCESS;
  }

  efi_status Write(size_t* len, const void* buf) {
    *len = fwrite(buf, 1, *len, stdio_file_);
    return EFI_SUCCESS;
  }

  efi_status GetPosition(uint64_t* position) {
    *position = ftell(stdio_file_);
    return EFI_SUCCESS;
  }

  efi_status SetPosition(uint64_t position) {
    return fseek(stdio_file_, position, SEEK_SET) == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  }

  efi_status GetInfo(const efi_guid* info_type, size_t* buf_size, void* buf);

  efi_status SetInfo(const efi_guid* info_type, size_t buf_size, void* buf);

  efi_status Flush() { return fflush(stdio_file_) == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR; }

  efi_status OpenEx(struct efi_file_protocol* new_handle, char16_t* filename, uint64_t open_mode,
                    uint64_t attributes, efi_file_io_token* token) {
    return EFI_UNSUPPORTED;
  }

  efi_status ReadEx(efi_file_io_token* token) { return EFI_UNSUPPORTED; }

  efi_status WriteEx(efi_file_io_token* token) { return EFI_UNSUPPORTED; }

  efi_status FlushEx(efi_file_io_token* token) { return EFI_UNSUPPORTED; }

 private:
  FILE* stdio_file_ = nullptr;
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STDIO_FILE_PROTOCOL_H_
