// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xefi.h"

#include <lib/efi/testing/mock_serial_io.h>
#include <lib/efi/testing/mock_simple_text_input.h>
#include <lib/efi/testing/stub_boot_services.h>

#include <efi/protocol/serial-io.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::efi::MatchGuid;
using ::efi::MockBootServices;
using ::efi::MockSerialIoProtocol;
using ::efi::MockSimpleTextInputProtocol;
using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::Test;

const efi_handle kImageHandle = reinterpret_cast<efi_handle>(0x10);

// Test fixture to set up and tear down XEFI state.
class XefiTest : public Test {
 public:
  // Reset xefi global variables so state doesn't bleed between tests.
  void TearDown() override { memset(&xefi_global_state, 0, sizeof(xefi_global_state)); }

  // Sets up the state and mock expectations for a future call to xefi_init().
  void SetupXefi(MockBootServices& mock_services, efi_serial_io_protocol* serial,
                 efi_simple_text_input_protocol* text_input = nullptr) {
    system_table_ = efi_system_table{
        .ConIn = text_input,
        .ConOut = &output_protocol_,
        .BootServices = mock_services.services(),
    };

    if (serial) {
      EXPECT_CALL(mock_services, LocateProtocol(MatchGuid(EFI_SERIAL_IO_PROTOCOL_GUID), _, _))
          .WillOnce(DoAll(SetArgPointee<2>(serial), Return(EFI_SUCCESS)));
    } else {
      EXPECT_CALL(mock_services, LocateProtocol(MatchGuid(EFI_SERIAL_IO_PROTOCOL_GUID), _, _))
          .WillOnce(Return(EFI_LOAD_ERROR));
    }
  }

 protected:
  efi_simple_text_output_protocol output_protocol_;
  efi_system_table system_table_;
};

TEST_F(XefiTest, InitWithoutSerial) {
  MockBootServices mock_services;
  SetupXefi(mock_services, nullptr);

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(gSys, &system_table_);
  EXPECT_EQ(gImg, kImageHandle);
  EXPECT_EQ(gBS, mock_services.services());
  EXPECT_EQ(gConOut, &output_protocol_);
  EXPECT_EQ(gSerial, nullptr);
}

TEST_F(XefiTest, InitWithSerial) {
  MockBootServices mock_services;
  efi_serial_io_protocol serial_protocol;
  SetupXefi(mock_services, &serial_protocol);

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(gSys, &system_table_);
  EXPECT_EQ(gImg, kImageHandle);
  EXPECT_EQ(gBS, mock_services.services());
  EXPECT_EQ(gConOut, &output_protocol_);
  EXPECT_EQ(gSerial, &serial_protocol);
}

TEST_F(XefiTest, GetcSerialPoll) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_input, ReadKeyStroke).WillRepeatedly(Return(EFI_NOT_READY));
  mock_serial.ExpectRead("x");

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ('x', xefi_getc(0));
}

TEST_F(XefiTest, GetcInputPoll) {
  MockBootServices mock_services;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, nullptr, mock_input.protocol());

  mock_input.ExpectReadKeyStroke('z');

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ('z', xefi_getc(0));
}

TEST_F(XefiTest, GetcInputTakesPrecedence) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_serial, Read).Times(0);
  mock_input.ExpectReadKeyStroke('z');

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ('z', xefi_getc(0));
}

TEST_F(XefiTest, GetcPollNoCharacter) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_serial, Read).WillOnce(Return(EFI_TIMEOUT));
  EXPECT_CALL(mock_input, ReadKeyStroke).WillOnce(Return(EFI_NOT_READY));

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(-1, xefi_getc(0));
}

TEST_F(XefiTest, GetcTimer) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  // Mock 3 "not ready" loops, then find a character on the 4th.
  EXPECT_CALL(mock_services, CreateEvent(EVT_TIMER, _, _, _, _)).WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, SetTimer(_, TimerRelative, _)).WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, CheckEvent(_)).Times(3).WillRepeatedly(Return(EFI_NOT_READY));
  EXPECT_CALL(mock_services, CloseEvent(_)).WillOnce(Return(EFI_SUCCESS));

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_serial, Read).Times(3).WillRepeatedly(Return(EFI_TIMEOUT));

  {
    InSequence seq;
    EXPECT_CALL(mock_input, ReadKeyStroke).Times(3).WillRepeatedly(Return(EFI_NOT_READY));
    mock_input.ExpectReadKeyStroke('z');
  }

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ('z', xefi_getc(100));
}

TEST_F(XefiTest, GetcTimeout) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  // Mock 2 "not ready" loops, then timeout on the 3rd.
  EXPECT_CALL(mock_services, CreateEvent(EVT_TIMER, _, _, _, _)).WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, SetTimer(_, TimerRelative, _)).WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, CheckEvent(_))
      .WillOnce(Return(EFI_NOT_READY))
      .WillOnce(Return(EFI_NOT_READY))
      .WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, CloseEvent(_)).WillOnce(Return(EFI_SUCCESS));

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_serial, Read).Times(3).WillRepeatedly(Return(EFI_TIMEOUT));

  EXPECT_CALL(mock_input, ReadKeyStroke).Times(3).WillRepeatedly(Return(EFI_NOT_READY));

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(-1, xefi_getc(100));
}

TEST_F(XefiTest, SerialAttributesFailure) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillOnce(Return(EFI_DEVICE_ERROR));

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(-1, xefi_getc(0));
}

TEST_F(XefiTest, CreateTimerFailure) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, CreateEvent(EVT_TIMER, _, _, _, _))
      .WillOnce(Return(EFI_OUT_OF_RESOURCES));

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(-1, xefi_getc(100));
}

TEST_F(XefiTest, SetTimerFailure) {
  MockBootServices mock_services;
  MockSerialIoProtocol mock_serial;
  MockSimpleTextInputProtocol mock_input;
  SetupXefi(mock_services, mock_serial.protocol(), mock_input.protocol());

  EXPECT_CALL(mock_serial, SetAttributes).WillRepeatedly(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, CreateEvent(EVT_TIMER, _, _, _, _)).WillOnce(Return(EFI_SUCCESS));
  EXPECT_CALL(mock_services, SetTimer(_, TimerRelative, _)).WillOnce(Return(EFI_INVALID_PARAMETER));
  EXPECT_CALL(mock_services, CloseEvent(_)).WillOnce(Return(EFI_SUCCESS));

  xefi_init(kImageHandle, &system_table_);
  EXPECT_EQ(-1, xefi_getc(100));
}

}  // namespace
