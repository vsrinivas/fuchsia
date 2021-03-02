// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xefi.h"

#include <lib/efi/testing/stub_boot_services.h>

#include <efi/protocol/serial-io.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::efi::MatchGuid;
using ::efi::MockBootServices;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::Test;

const efi_handle kImageHandle = reinterpret_cast<efi_handle>(0x10);

// Test fixture to automatically reset xefi global variables after each test
// to make sure state doesn't bleed between them.
class XefiTest : public Test {
 public:
  void TearDown() override { memset(&xefi_global_state, 0, sizeof(xefi_global_state)); }
};

TEST_F(XefiTest, InitWithoutSerial) {
  MockBootServices mock_services;
  efi_simple_text_output_protocol output_protocol;
  efi_system_table system_table = {
      .ConOut = &output_protocol,
      .BootServices = mock_services.services(),
  };

  EXPECT_CALL(mock_services, LocateProtocol(MatchGuid(EFI_SERIAL_IO_PROTOCOL_GUID), _, _))
      .WillOnce(Return(EFI_LOAD_ERROR));

  xefi_init(kImageHandle, &system_table);
  EXPECT_EQ(gSys, &system_table);
  EXPECT_EQ(gImg, kImageHandle);
  EXPECT_EQ(gBS, mock_services.services());
  EXPECT_EQ(gConOut, &output_protocol);
  EXPECT_EQ(gSerial, nullptr);
}

TEST_F(XefiTest, InitWithSerial) {
  MockBootServices mock_services;
  efi_simple_text_output_protocol output_protocol;
  efi_system_table system_table = {
      .ConOut = &output_protocol,
      .BootServices = mock_services.services(),
  };

  efi_serial_io_protocol serial_protocol;
  EXPECT_CALL(mock_services, LocateProtocol(MatchGuid(EFI_SERIAL_IO_PROTOCOL_GUID), _, _))
      .WillOnce(DoAll(SetArgPointee<2>(&serial_protocol), Return(EFI_SUCCESS)));

  xefi_init(kImageHandle, &system_table);
  EXPECT_EQ(gSys, &system_table);
  EXPECT_EQ(gImg, kImageHandle);
  EXPECT_EQ(gBS, mock_services.services());
  EXPECT_EQ(gConOut, &output_protocol);
  EXPECT_EQ(gSerial, &serial_protocol);
}

}  // namespace
