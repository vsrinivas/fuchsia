// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/efi/file.h"

#include <zircon/assert.h>

#include <efi/protocol/file.h>
#include <efi/protocol/simple-file-system.h>
#include <fbl/alloc_checker.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>
#include <phys/efi/main.h>
#include <phys/efi/protocol.h>
#include <phys/symbolize.h>

#include "src/lib/utf_conversion/utf_conversion.h"

#include <ktl/enforce.h>

template <>
constexpr const efi_guid& kEfiProtocolGuid<efi_simple_file_system_protocol> =
    SimpleFileSystemProtocol;

namespace {

ktl::unique_ptr<char16_t[]> ConvertUtf8ToUtf16CString(ktl::string_view utf8) {
  fbl::AllocChecker ac;
  ktl::unique_ptr<char16_t[]> utf16(new (&ac) char16_t[utf8.size() + 1]);
  if (!ac.check()) {
    return nullptr;
  }
  size_t len = utf8.size();

  zx_status_t status = utf8_to_utf16(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(),
                                     reinterpret_cast<uint16_t*>(utf16.get()), &len);
  if (status != ZX_OK) {
    printf("%s: Error %d converting UTF8 file name \"%.*s\" to UTF16!\n", ProgramName(), status,
           static_cast<int>(utf8.size()), utf8.data());
    return nullptr;
  }
  ZX_ASSERT_MSG(len <= utf8.size(), "%zu UTF8 became %zu UTF16??", utf8.size(), len);
  utf16[len] = L'\0';
  return utf16;
}

}  // namespace

EfiFilePtr EfiRootDir() {
  if (!gEfiLoadedImage) {
    printf("%s: Cannot get EFI root filesystem without LOADED_IMAGE_PROTOCOL\n", ProgramName());
    return {};
  }
  auto fs = EfiOpenProtocol<efi_simple_file_system_protocol>(gEfiLoadedImage->DeviceHandle);
  if (fs.is_error()) {
    printf("%s: EFI error %#zx getting SIMPLE_FILE_SYSTEM_PROTOCOL\n", ProgramName(),
           fs.error_value());
  }

  efi_file_protocol* root = nullptr;
  efi_status status = fs->OpenVolume(fs.value().get(), &root);
  if (status != EFI_SUCCESS) {
    printf("%s: EFI error %#zx from OpenVolume", ProgramName(), status);
    return {};
  }

  return EfiFilePtr(root);
}

fit::result<efi_status, uint64_t> EfiFileSize(efi_file_protocol* file) {
  union {
    efi_file_info info;
    char space[sizeof(efi_file_info) + sizeof(char16_t[255])];
  } buffer;
  size_t info_size = sizeof(buffer);
  efi_status status = file->GetInfo(file, &FileInfoGuid, &info_size, &buffer);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  ZX_ASSERT(info_size >= sizeof(buffer.info));
  return fit::ok(buffer.info.FileSize);
}

fit::result<efi_status, EfiFilePtr> EfiOpenFile(const char16_t* filename, efi_file_protocol* dir) {
  efi_file_protocol* file = nullptr;
  efi_status status = dir->Open(dir, &file, filename, EFI_FILE_MODE_READ, 0);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok(EfiFilePtr(file));
}

fit::result<efi_status, EfiFilePtr> EfiOpenFile(ktl::string_view filename, efi_file_protocol* dir) {
  ktl::unique_ptr<char16_t[]> utf16 = ConvertUtf8ToUtf16CString(filename);
  if (!utf16) {
    return fit::error{EFI_OUT_OF_RESOURCES};
  }
  return EfiOpenFile(utf16.get(), dir);
}
