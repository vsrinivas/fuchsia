// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERVICE_BINDING_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERVICE_BINDING_H_

#include <efi/protocol/service-binding.h>
#include <gmock/gmock.h>

#include "mock_protocol_base.h"

namespace efi {

// gmock wrapper for efi_service_binding_protocol.
class MockServiceBindingProtocol
    : public MockProtocolBase<MockServiceBindingProtocol, efi_service_binding_protocol> {
 public:
  MockServiceBindingProtocol()
      : MockProtocolBase({.CreateChild = Bounce<&MockServiceBindingProtocol::CreateChild>,
                          .DestroyChild = Bounce<&MockServiceBindingProtocol::DestroyChild>}) {}

  MOCK_METHOD(efi_status, CreateChild, (efi_handle * child_handle));
  MOCK_METHOD(efi_status, DestroyChild, (efi_handle child_handle));
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_MOCK_SERVICE_BINDING_H_
