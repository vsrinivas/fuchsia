// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stub_boot_services.h>
#include <stdio.h>

#include <efi/protocol/block-io.h>
#include <gtest/gtest.h>

namespace efi {

namespace {

using ::testing::_;
using ::testing::Return;

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
  constexpr size_t kPoolSize = 16;

  void* memory = nullptr;
  ASSERT_EQ(EFI_SUCCESS, stub.services()->AllocatePool(EfiConventionalMemory, kPoolSize, &memory));
  ASSERT_NE(nullptr, memory);

  // Make sure we initialized the memory to something nonzero.
  for (size_t i = 0; i < kPoolSize; ++i) {
    EXPECT_NE(0, reinterpret_cast<uint8_t*>(memory)[i]);
  }

  ASSERT_EQ(EFI_SUCCESS, stub.services()->FreePool(memory));
}

const efi_handle kTestHandle = reinterpret_cast<efi_handle>(0x10);

TEST(MockBootServices, ExpectProtocol) {
  constexpr efi_guid guid = EFI_BLOCK_IO_PROTOCOL_GUID;
  efi_block_io_protocol protocol;

  MockBootServices mock;
  mock.ExpectProtocol(kTestHandle, guid, &protocol);

  void* protocol_out = nullptr;
  EXPECT_EQ(EFI_SUCCESS,
            mock.services()->OpenProtocol(kTestHandle, &guid, &protocol_out, nullptr, nullptr, 0));
  EXPECT_EQ(protocol_out, &protocol);
  EXPECT_EQ(EFI_SUCCESS, mock.services()->CloseProtocol(kTestHandle, &guid, nullptr, nullptr));
}

TEST(MockBootServices, ExpectOpenProtocol) {
  constexpr efi_guid guid = EFI_BLOCK_IO_PROTOCOL_GUID;
  efi_block_io_protocol protocol;

  MockBootServices mock;
  mock.ExpectOpenProtocol(kTestHandle, guid, &protocol);

  void* protocol_out = nullptr;
  EXPECT_EQ(EFI_SUCCESS,
            mock.services()->OpenProtocol(kTestHandle, &guid, &protocol_out, nullptr, nullptr, 0));
  EXPECT_EQ(protocol_out, &protocol);
}

TEST(MockBootServices, ExpectCloseProtocol) {
  constexpr efi_guid guid = EFI_BLOCK_IO_PROTOCOL_GUID;

  MockBootServices mock;
  mock.ExpectCloseProtocol(kTestHandle, guid);

  EXPECT_EQ(EFI_SUCCESS, mock.services()->CloseProtocol(kTestHandle, &guid, nullptr, nullptr));
}

TEST(StubBootServices, LocateProtocol) {
  constexpr efi_guid protocol_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
  void* protocol = nullptr;

  MockBootServices mock;
  EXPECT_CALL(mock, LocateProtocol(&protocol_guid, _, &protocol)).WillOnce(Return(EFI_TIMEOUT));

  EXPECT_EQ(EFI_TIMEOUT, mock.services()->LocateProtocol(&protocol_guid, nullptr, &protocol));
}

}  // namespace

}  // namespace efi
