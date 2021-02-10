// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stub_boot_services.h>
#include <stdio.h>

#include <efi/protocol/block-io.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace efi {

namespace {

using testing::_;
using testing::Return;

// Creating a second is OK as long as the first has gone out of scope.
TEST(StubBootServices, CreateTwice) {
  StubBootServices();
  StubBootServices();
}

// Creating a second while the first is still up is a fatal error.
TEST(StubBootServicesDeathTest, CreateDuplicate) {
  StubBootServices stub;
  ASSERT_DEATH(StubBootServices(), "");
}

TEST(StubBootServices, AllocateAndFreePool) {
  StubBootServices stub;

  void* memory = nullptr;
  ASSERT_EQ(EFI_SUCCESS, stub.services()->AllocatePool(EfiConventionalMemory, 16, &memory));
  ASSERT_NE(nullptr, memory);

  ASSERT_EQ(EFI_SUCCESS, stub.services()->FreePool(memory));
}

// Make sure mocking out the class works as expected. This ensures our code to
// bounce the C callback into the C++ class is working properly.
class MockBootServices : public StubBootServices {
 public:
  MOCK_METHOD(efi_status, OpenProtocol,
              (efi_handle handle, efi_guid* protocol, void** intf, efi_handle agent_handle,
               efi_handle controller_handle, uint32_t attributes),
              (override));
  MOCK_METHOD(efi_status, CloseProtocol,
              (efi_handle handle, efi_guid* protocol, efi_handle agent_handle,
               efi_handle controller_handle),
              (override));
  MOCK_METHOD(efi_status, LocateHandleBuffer,
              (efi_locate_search_type search_type, efi_guid* protocol, void* search_key,
               size_t* num_handles, efi_handle** buf),
              (override));
};

const efi_handle kTestHandle1 = reinterpret_cast<efi_handle>(0x1);
const efi_handle kTestHandle2 = reinterpret_cast<efi_handle>(0x2);
const efi_handle kTestHandle3 = reinterpret_cast<efi_handle>(0x3);

TEST(StubBootServices, OpenProtocol) {
  MockBootServices mock;
  efi_guid protocol_guid = EFI_BLOCK_IO_PROTOCOL_GUID;

  // Expect any non-zero return value so we can check that it worked.
  EXPECT_CALL(mock,
              OpenProtocol(kTestHandle1, &protocol_guid, _, kTestHandle2, kTestHandle3, 0xABCD))
      .WillOnce(Return(EFI_TIMEOUT));

  void* arg = nullptr;
  EXPECT_EQ(EFI_TIMEOUT, mock.services()->OpenProtocol(kTestHandle1, &protocol_guid, &arg,
                                                       kTestHandle2, kTestHandle3, 0xABCD));
}

TEST(StubBootServices, CloseProtocol) {
  MockBootServices mock;
  efi_guid protocol_guid = EFI_BLOCK_IO_PROTOCOL_GUID;

  EXPECT_CALL(mock, CloseProtocol(kTestHandle1, &protocol_guid, kTestHandle2, kTestHandle3))
      .WillOnce(Return(EFI_TIMEOUT));

  EXPECT_EQ(EFI_TIMEOUT, mock.services()->CloseProtocol(kTestHandle1, &protocol_guid, kTestHandle2,
                                                        kTestHandle3));
}

TEST(StubBootServices, LocateHandleBuffer) {
  MockBootServices mock;
  efi_guid protocol_guid = EFI_BLOCK_IO_PROTOCOL_GUID;

  EXPECT_CALL(mock, LocateHandleBuffer(ByProtocol, &protocol_guid, _, _, _))
      .WillOnce(Return(EFI_TIMEOUT));

  EXPECT_EQ(EFI_TIMEOUT, mock.services()->LocateHandleBuffer(ByProtocol, &protocol_guid, nullptr,
                                                             nullptr, nullptr));
}

}  // namespace

}  // namespace efi
