// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_UTILS_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_UTILS_H_

#include <lib/stdcompat/span.h>

#include <optional>

#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/tcg2.h>
#include <efi/types.h>
#include <phys/efi/protocol.h>

#include "phys/efi/main.h"

template <>
inline constexpr const efi_guid& kEfiProtocolGuid<efi_device_path_protocol> = DevicePathProtocol;

template <>
inline constexpr const efi_guid& kEfiProtocolGuid<efi_block_io_protocol> = BlockIoProtocol;

template <>
inline constexpr const efi_guid& kEfiProtocolGuid<efi_disk_io_protocol> = DiskIoProtocol;

template <>
inline constexpr const efi_guid& kEfiProtocolGuid<efi_tcg2_protocol> = Tcg2Protocol;

namespace gigaboot {

// This calls into the LocateProtocol() boot service. It returns a null pointer if operation fails.
template <class Protocol>
inline fit::result<efi_status, EfiProtocolPtr<Protocol>> EfiLocateProtocol() {
  void* ptr = nullptr;
  efi_status status =
      gEfiSystemTable->BootServices->LocateProtocol(&kEfiProtocolGuid<Protocol>, nullptr, &ptr);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }
  return fit::ok(EfiProtocolPtr<Protocol>(static_cast<Protocol*>(ptr)));
}

// A wrapper type for the list of handles returned by LocateHandleBuffer() boot service. It owns
// the memory backing the list and will free it upon destruction
class HandleBuffer {
 public:
  HandleBuffer(efi_handle* handles, size_t count) : handles_(handles), count_(count) {}
  cpp20::span<efi_handle> AsSpan() { return cpp20::span<efi_handle>{handles_.get(), count_}; }

 private:
  // The deleter frees the list by calling the FreePool() boot service.
  struct HandlePtrDeleter {
    void operator()(efi_handle* ptr) { gEfiSystemTable->BootServices->FreePool(ptr); }
  };

  std::unique_ptr<efi_handle, HandlePtrDeleter> handles_;
  const size_t count_ = 0;
};

// This calls into LocateHandleBuffer() with ByProtocol search type and the given protocol.
// It returns a list of efi_handles that support the given protocol
template <class Protocol>
inline fit::result<efi_status, HandleBuffer> EfiLocateHandleBufferByProtocol() {
  size_t count;
  efi_handle* handles;
  efi_status status = gEfiSystemTable->BootServices->LocateHandleBuffer(
      ByProtocol, &kEfiProtocolGuid<Protocol>, nullptr, &count, &handles);
  if (status != EFI_SUCCESS) {
    return fit::error{status};
  }

  return fit::ok(HandleBuffer(handles, count));
}

// Convert a given efi_status code to informative string.
const char* EfiStatusToString(efi_status status);

// Convert efi memory type code to zbi memory type code.
uint32_t EfiToZbiMemRangeType(uint32_t efi_mem_type);

// Convert an integer to big endian byte order
uint64_t ToBigEndian(uint64_t val);

// Convert an given integer, assuming in big endian to little endian order.
uint64_t BigToHostEndian(uint64_t val);

constexpr size_t kUefiPageSize = 4096;

efi_status PrintTpm2Capability();

// Check whether secure boot is turned on by querying the `SecureBoot` global variable.
// Returns error if fail to query `SecureBoot`.
fit::result<efi_status, bool> IsSecureBootOn();

enum class RebootMode : uint8_t {
  kNormal = 0x1,
  kRecovery = 0x2,
  kBootloader = 0x4,
  kBootloaderDefault = 0xFF,
};

constexpr uint8_t RebootModeToByte(RebootMode m) { return static_cast<uint8_t>(m); }

// Set reboot mode. Returns true if succeeds, false otherwise.
bool SetRebootMode(RebootMode mode);

// Get reboot mode.
// Returns std::nullopt on failure
std::optional<RebootMode> GetRebootMode();

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_UTILS_H_
