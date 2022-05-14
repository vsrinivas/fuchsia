// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_FILE_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_FILE_H_

#include <efi/protocol/file.h>
#include <gmock/gmock.h>

#include "mock_protocol_base.h"

namespace efi {

// gmock wrapper for efi_file_protocol.
class MockFileProtocol : public MockProtocolBase<MockFileProtocol, efi_file_protocol> {
 public:
  MockFileProtocol()
      : MockProtocolBase<MockFileProtocol, efi_file_protocol>({
            .Revision = EFI_FILE_PROTOCOL_LATEST_REVISION,
            .Open = Bounce<&MockFileProtocol::Open>,
            .Close = Bounce<&MockFileProtocol::Close>,
            .Delete = Bounce<&MockFileProtocol::Delete>,
            .Read = Bounce<&MockFileProtocol::Read>,
            .Write = Bounce<&MockFileProtocol::Write>,
            .GetPosition = Bounce<&MockFileProtocol::GetPosition>,
            .SetPosition = Bounce<&MockFileProtocol::SetPosition>,
            .GetInfo = Bounce<&MockFileProtocol::GetInfo>,
            .SetInfo = Bounce<&MockFileProtocol::SetInfo>,
            .Flush = Bounce<&MockFileProtocol::Flush>,
            .OpenEx = Bounce<&MockFileProtocol::OpenEx>,
            .ReadEx = Bounce<&MockFileProtocol::ReadEx>,
            .WriteEx = Bounce<&MockFileProtocol::WriteEx>,
            .FlushEx = Bounce<&MockFileProtocol::FlushEx>,
        }) {}

  MOCK_METHOD(efi_status, Open,
              (efi_file_protocol * *new_handle, const char16_t* filename, uint64_t open_mode,
               uint64_t attributes));

  MOCK_METHOD(efi_status, Close, ());

  MOCK_METHOD(efi_status, Delete, ());

  MOCK_METHOD(efi_status, Read, (size_t * len, void* buf));

  MOCK_METHOD(efi_status, Write, (size_t * len, const void* buf));

  MOCK_METHOD(efi_status, GetPosition, (uint64_t * position));

  MOCK_METHOD(efi_status, SetPosition, (uint64_t position));

  MOCK_METHOD(efi_status, GetInfo, (const efi_guid* info_type, size_t* buf_size, void* buf));

  MOCK_METHOD(efi_status, SetInfo, (const efi_guid* info_type, size_t buf_size, void* buf));

  MOCK_METHOD(efi_status, Flush, ());

  MOCK_METHOD(efi_status, OpenEx,
              (struct efi_file_protocol * new_handle, char16_t* filename, uint64_t open_mode,
               uint64_t attributes, efi_file_io_token* token));

  MOCK_METHOD(efi_status, ReadEx, (efi_file_io_token * token));

  MOCK_METHOD(efi_status, WriteEx, (efi_file_io_token * token));

  MOCK_METHOD(efi_status, FlushEx, (efi_file_io_token * token));
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_FILE_H_
