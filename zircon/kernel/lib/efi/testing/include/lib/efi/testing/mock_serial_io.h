// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERIAL_IO_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERIAL_IO_H_

#include <string>
#include <type_traits>

#include <efi/protocol/serial-io.h>
#include <gmock/gmock.h>

#include "mock_protocol_base.h"

namespace efi {

// gmock wrapper for efi_serial_io_protocol.
class MockSerialIoProtocol : public MockProtocolBase<MockSerialIoProtocol, efi_serial_io_protocol> {
 public:
  MockSerialIoProtocol()
      : MockProtocolBase({.Reset = Bounce<&MockSerialIoProtocol::Reset>,
                          .SetAttributes = Bounce<&MockSerialIoProtocol::SetAttributes>,
                          .SetControl = Bounce<&MockSerialIoProtocol::SetControl>,
                          .GetControl = Bounce<&MockSerialIoProtocol::GetControl>,
                          .Write = Bounce<&MockSerialIoProtocol::Write>,
                          .Read = Bounce<&MockSerialIoProtocol::Read>,
                          .Mode = &mode_}) {}

  MOCK_METHOD(efi_status, Reset, ());
  MOCK_METHOD(efi_status, SetAttributes,
              (uint64_t BaudRate, uint32_t ReceiveFifoDepth, uint32_t Timeout,
               efi_parity_type Parity, uint8_t DataBits, efi_stop_bits_type StopBits));
  MOCK_METHOD(efi_status, SetControl, (uint32_t Control));
  MOCK_METHOD(efi_status, GetControl, (uint32_t * Control));
  MOCK_METHOD(efi_status, Write, (uint64_t * BufferSize, void* Buffer));
  MOCK_METHOD(efi_status, Read, (uint64_t * BufferSize, void* Buffer));

  // Sets up expectations for Read() to return the given chars.
  // |input| gets saved internally so does not need to be kept alive.
  void ExpectRead(const std::string& input) {
    EXPECT_CALL(*this, Read(::testing::Pointee(::testing::Ge(input.size())), ::testing::_))
        .WillOnce([input](uint64_t* BufferSize, void* Buffer) {
          *BufferSize = input.size();
          memcpy(Buffer, input.data(), input.size());
          return EFI_SUCCESS;
        });
  }

 private:
  serial_io_mode mode_;
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERIAL_IO_H_
