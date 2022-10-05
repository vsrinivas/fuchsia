// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_EFI_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_EFI_H_

// Including this header makes zbitl::View<efi_file_protocol*> and
// zbitl::Image<efi_file_protocol*> specializations available to access ZBI
// files via the EFI filesystem.  For smart-pointer types for efi_file_protocol
// (using unique_ptr, as in <phys/efi/file.h> EfiFilePtr), there is also an
// owning specialization.

#include <memory>

#include <efi/protocol/file.h>

#include "storage-traits.h"

namespace zbitl {

template <>
class StorageTraits<efi_file_protocol*> {
 public:
  using error_type = efi_status;

  static std::string_view error_string(error_type error) {
    // TODO(mcgrathr): efi error strings
    return "<EFI error>";
  }

  /// Offset into file where the ZBI item payload begins.
  using payload_type = uint64_t;

  static fit::result<error_type, uint32_t> Capacity(efi_file_protocol* file);

  static fit::result<error_type> EnsureCapacity(efi_file_protocol* file, uint32_t capacity_bytes);

  static fit::result<error_type, payload_type> Payload(efi_file_protocol* file, uint32_t offset,
                                                       uint32_t length) {
    return fit::ok(offset);
  }

  static fit::result<error_type> Read(efi_file_protocol* file, payload_type payload, void* buffer,
                                      uint32_t length);

  template <typename Callback>
  static auto Read(efi_file_protocol* file, payload_type payload, uint32_t length,
                   Callback&& callback) -> fit::result<error_type, decltype(callback(ByteView{}))> {
    std::optional<decltype(callback(ByteView{}))> result;
    auto cb = [&](ByteView chunk) -> bool {
      result = callback(chunk);
      return result->is_ok();
    };
    using CbType = decltype(cb);
    auto read_error = DoRead(
        file, payload, length,
        [](void* cb, ByteView chunk) { return (*static_cast<CbType*>(cb))(chunk); }, &cb);
    if (read_error.is_error()) {
      return read_error.take_error();
    }
    ZX_DEBUG_ASSERT(result);
    return fit::ok(*result);
  }

  static fit::result<error_type> Write(efi_file_protocol* file, uint32_t offset, ByteView data);

 private:
  static fit::result<error_type> DoRead(efi_file_protocol* f, payload_type offset, uint32_t length,
                                        bool (*)(void*, ByteView), void*);
};

template <class Deleter>
class StorageTraits<std::unique_ptr<efi_file_protocol, Deleter>>
    : public StorageTraits<efi_file_protocol*> {
 public:
  using Base = StorageTraits<efi_file_protocol*>;

  static fit::result<error_type, uint32_t> Capacity(
      const std::unique_ptr<efi_file_protocol, Deleter>& file) {
    return Base::Capacity(file.get());
  }

  static fit::result<error_type> EnsureCapacity(
      const std::unique_ptr<efi_file_protocol, Deleter>& file, uint32_t capacity_bytes) {
    return Base::EnsureCapacity(file.get(), capacity_bytes);
  }

  static fit::result<error_type, payload_type> Payload(
      const std::unique_ptr<efi_file_protocol, Deleter>& file, uint32_t offset, uint32_t length) {
    return Base::Payload(file.get(), offset, length);
  }

  static fit::result<error_type> Read(const std::unique_ptr<efi_file_protocol, Deleter>& file,
                                      payload_type payload, void* buffer, uint32_t length) {
    return Base::Read(file.get(), payload, buffer, length);
  }

  static fit::result<error_type> Write(const std::unique_ptr<efi_file_protocol, Deleter>& file,
                                       uint32_t offset, ByteView data) {
    return Base::Write(file.get(), offset, data);
  }
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_EFI_H_
