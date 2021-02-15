// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_FAKE_DISK_IO_PROTOCOL_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_FAKE_DISK_IO_PROTOCOL_H_

#include <map>
#include <type_traits>
#include <vector>

#include <efi/protocol/disk-io.h>

namespace efi {

// Wraps efi_disk_io_protocol to support reading/writing a fake disk.
//
// Example usage:
//   FakeDiskIoProtocol fake;
//
//   // Register media 5 with some contents.
//   fake.contents(5) = {0x00, 0x88, 0xFF};
//
//   // Functions that read using the EFI protocol will see the above contents.
//   ASSERT_OK(my_test_read_func(fake.protocol()));
//
//   // Functions that write using the EFI protocol will update the contents.
//   ASSERT_OK(my_test_write_func(fake.protocol()));
//   ASSERT_EQ(std::vector<uint8_t>{0xAA, 0xAA, 0xAA}, fake.contents(5));
class FakeDiskIoProtocol {
 public:
  FakeDiskIoProtocol();

  // Not copyable or movable.
  FakeDiskIoProtocol(const FakeDiskIoProtocol&) = delete;
  FakeDiskIoProtocol& operator=(const FakeDiskIoProtocol&) = delete;

  // Returns the test disk contents for the given MediaId.
  //
  // This can be modified to change what the disk looks like on subsequent
  // reads. If the given MediaId doesn't exist yet, a new empty disk will
  // be created and returned.
  //
  // Trying to use the EFI APIs to read/write from a MediaId before it has
  // been created here will fail.
  std::vector<uint8_t>& contents(uint32_t MediaId) { return media_contents_[MediaId]; }

  // Returns the underlying efi_disk_io_protocol struct.
  // Needs to be non-const to pass into the EFI functions, but callers should
  // not modify the struct.
  efi_disk_io_protocol* protocol() { return &protocol_; }

 private:
  // Static wrappers to bounce the C function call into our C++ object.
  static efi_status ReadDiskWrapper(efi_disk_io_protocol* self, uint32_t MediaId, uint64_t Offset,
                                    uint64_t BufferSize, void* Buffer) EFIAPI;
  static efi_status WriteDiskWrapper(efi_disk_io_protocol* self, uint32_t MediaId, uint64_t Offset,
                                     uint64_t BufferSize, const void* Buffer) EFIAPI;

  // EFI read/write function implementation.
  efi_status ReadDisk(uint32_t MediaId, uint64_t Offset, uint64_t BufferSize, void* Buffer);
  efi_status WriteDisk(uint32_t MediaId, uint64_t Offset, uint64_t BufferSize, const void* Buffer);

  // Validates read/write params.
  efi_status ValidateParams(uint32_t MediaId, uint64_t Offset, uint64_t BufferSize);

  // Note: this MUST be the first data member in the class so that we can
  // convert efi_disk_io_protocol* <-> FakeDiskIoProtocol*.
  efi_disk_io_protocol protocol_;

  // Maps a MediaId param to its fake contents.
  std::map<uint32_t, std::vector<uint8_t>> media_contents_;
};

// If this fails, we either need to modify our class layout or change how we
// convert between efi_disk_io_protocol and FakeDiskIoProtocol.
static_assert(std::is_standard_layout_v<FakeDiskIoProtocol>,
              "FakeDiskIoProtocol is not standard layout");

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_FAKE_DISK_IO_PROTOCOL_H_
