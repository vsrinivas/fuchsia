// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_FILE_H_
#define ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_FILE_H_

#include <lib/fit/result.h>

#include <memory>
#include <string_view>

#include <efi/protocol/file.h>
#include <efi/types.h>

// Custom unique_ptr deleter used by EfiFilePtr; see below.
struct EfiFilePtrDeleter {
  void operator()(efi_file_protocol* file) const { file->Close(file); }
};

// EfiFilePtr is a smart-pointer type for efi_file_protocol pointers.
using EfiFilePtr = std::unique_ptr<efi_file_protocol, EfiFilePtrDeleter>;

// Get the directory handle for the root directory of the UEFI filesystem
// from which this UEFI application was launched.
EfiFilePtr EfiRootDir();

// Open the named file (for reading) within the (optionally) given directory,
// the default being the root directory EfiRootDir() finds.
fit::result<efi_status, EfiFilePtr> EfiOpenFile(const char16_t* filename,
                                                efi_file_protocol* dir = EfiRootDir().get());
fit::result<efi_status, EfiFilePtr> EfiOpenFile(std::string_view filename,
                                                efi_file_protocol* dir = EfiRootDir().get());

// Determine the size of the file in bytes.
fit::result<efi_status, uint64_t> EfiFileSize(efi_file_protocol* file);

inline fit::result<efi_status, uint64_t> EfiFileSize(const EfiFilePtr& file) {
  return EfiFileSize(file.get());
}

#endif  // ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_FILE_H_
