// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SIMPLE_TEXT_INPUT_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SIMPLE_TEXT_INPUT_H_

#include <type_traits>

#include <efi/protocol/simple-text-input.h>
#include <gmock/gmock.h>

#include "mock_protocol_base.h"

namespace efi {

// gmock wrapper for efi_simple_text_input_protocol.
class MockSimpleTextInputProtocol
    : public MockProtocolBase<MockSimpleTextInputProtocol, efi_simple_text_input_protocol> {
 public:
  MockSimpleTextInputProtocol()
      : MockProtocolBase({.Reset = Bounce<&MockSimpleTextInputProtocol::Reset>,
                          .ReadKeyStroke = Bounce<&MockSimpleTextInputProtocol::ReadKeyStroke>}) {}

  MOCK_METHOD(efi_status, Reset, (bool extendend_verification));
  MOCK_METHOD(efi_status, ReadKeyStroke, (efi_input_key * key));

  // Sets up expectations for ReadKeyStroke() to return the given char.
  void ExpectReadKeyStroke(char input) {
    EXPECT_CALL(*this, ReadKeyStroke).WillOnce([input](efi_input_key* key) {
      key->ScanCode = 0;
      key->UnicodeChar = input;
      return EFI_SUCCESS;
    });
  }
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SIMPLE_TEXT_INPUT_H_
