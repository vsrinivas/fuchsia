// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/fake_disk_io_protocol.h>

namespace efi {

FakeDiskIoProtocol::FakeDiskIoProtocol()
    : protocol_{
          .Revision = EFI_DISK_IO_PROTOCOL_REVISION,
          .ReadDisk = ReadDiskWrapper,
          .WriteDisk = WriteDiskWrapper,
      } {}

efi_status FakeDiskIoProtocol::ReadDiskWrapper(efi_disk_io_protocol* self, uint32_t MediaId,
                                               uint64_t Offset, uint64_t BufferSize, void* Buffer) {
  return reinterpret_cast<FakeDiskIoProtocol*>(self)->ReadDisk(MediaId, Offset, BufferSize, Buffer);
}

efi_status FakeDiskIoProtocol::WriteDiskWrapper(efi_disk_io_protocol* self, uint32_t MediaId,
                                                uint64_t Offset, uint64_t BufferSize,
                                                const void* Buffer) {
  return reinterpret_cast<FakeDiskIoProtocol*>(self)->WriteDisk(MediaId, Offset, BufferSize,
                                                                Buffer);
}

efi_status FakeDiskIoProtocol::ReadDisk(uint32_t MediaId, uint64_t Offset, uint64_t BufferSize,
                                        void* Buffer) {
  efi_status status = ValidateParams(MediaId, Offset, BufferSize);
  if (status == EFI_SUCCESS) {
    memcpy(Buffer, &media_contents_[MediaId][Offset], BufferSize);
  }
  return status;
}

efi_status FakeDiskIoProtocol::WriteDisk(uint32_t MediaId, uint64_t Offset, uint64_t BufferSize,
                                         const void* Buffer) {
  efi_status status = ValidateParams(MediaId, Offset, BufferSize);
  if (status == EFI_SUCCESS) {
    memcpy(&media_contents_[MediaId][Offset], Buffer, BufferSize);
  }
  return status;
}

efi_status FakeDiskIoProtocol::ValidateParams(uint32_t MediaId, uint64_t Offset,
                                              uint64_t BufferSize) {
  auto iter = media_contents_.find(MediaId);
  if (iter == media_contents_.end()) {
    return EFI_NO_MEDIA;
  }

  const auto& contents = iter->second;
  if (Offset + BufferSize > contents.size()) {
    return EFI_END_OF_MEDIA;
  }

  return EFI_SUCCESS;
}

}  // namespace efi
