// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osboot.h"

#include <lib/efi/testing/mock_simple_text_input.h>
#include <lib/efi/testing/stub_boot_services.h>

#include <efi/protocol/simple-text-output.h>
#include <gtest/gtest.h>

#include "bootbyte.h"
#include "cmdline.h"
#include "xefi.h"

namespace {

using ::efi::MatchGuid;
using ::efi::MockBootServices;
using ::efi::MockSimpleTextInputProtocol;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Test;

const efi_handle kImageHandle = reinterpret_cast<efi_handle>(0x10);
const efi_event kTimerEvent = reinterpret_cast<efi_event>(0x80);

// If none of bootbyte, menu, or "bootloader.default" are provided, current
// default is netboot. See if we can switch this to local boot default?
constexpr BootAction kFallthroughBootAction = kBootActionNetboot;

// We don't have efi_simple_test_output_protocol mocks hooked up yet, for now
// just stub them out for simplicity since that's all we need.
EFIAPI efi_status StubEnableCursor(struct efi_simple_text_output_protocol*, bool) {
  return EFI_SUCCESS;
}
EFIAPI efi_status StubSetCursorPosition(struct efi_simple_text_output_protocol*, size_t, size_t) {
  return EFI_SUCCESS;
}

class GetBootActionTest : public Test {
 public:
  void SetUp() override {
    // Configure the necessary mocks for key_prompt().
    system_table_ = efi_system_table{
        .ConIn = mock_input_.protocol(),
        .ConOut = &output_protocol_,
        .BootServices = mock_services_.services(),
    };

    output_protocol_ = efi_simple_text_output_protocol{.SetCursorPosition = StubSetCursorPosition,
                                                       .EnableCursor = StubEnableCursor,
                                                       .Mode = &output_mode_};

    // Just use console in, no need for serial.
    EXPECT_CALL(mock_services_, LocateProtocol(MatchGuid(EFI_SERIAL_IO_PROTOCOL_GUID), _, _))
        .WillOnce(Return(EFI_LOAD_ERROR));

    xefi_init(kImageHandle, &system_table_);

    // Default behavior is to timeout without a key input.
    cmdline_set("bootloader.timeout", "1");
    ON_CALL(mock_input_, ReadKeyStroke).WillByDefault(Return(EFI_NOT_READY));
    ON_CALL(mock_services_, CreateEvent(EVT_TIMER, _, _, _, _))
        .WillByDefault([](uint32_t, efi_tpl, efi_event_notify, void*, efi_event* event) {
          // This doesn't have to point to real memory, but it has to be
          // non-NULL to make it look like the call succeeded.
          *event = kTimerEvent;
          return EFI_SUCCESS;
        });
    ON_CALL(mock_services_, CheckEvent(kTimerEvent)).WillByDefault(Return(EFI_SUCCESS));
  }

  void TearDown() override {
    // Reset all used state in between each test.
    bootbyte_clear();
    memset(&xefi_global_state, 0, sizeof(xefi_global_state));
    cmdline_clear();
  }

  void SetUserInput(char key) { mock_input_.ExpectReadKeyStroke(key); }

 protected:
  NiceMock<MockBootServices> mock_services_;
  NiceMock<MockSimpleTextInputProtocol> mock_input_;
  efi_simple_text_output_protocol output_protocol_ = {};
  simple_text_output_mode output_mode_ = {};
  efi_system_table system_table_ = {};
};

TEST_F(GetBootActionTest, BootbyteRecovery) {
  bootbyte_set_recovery();
  EXPECT_EQ(kBootActionSlotR, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, BootbyteBootloader) {
  bootbyte_set_bootloader();
  EXPECT_EQ(kBootActionFastboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, BootbyteNormal) {
  bootbyte_set_normal();
  EXPECT_EQ(kFallthroughBootAction, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, BootbyteDefault) {
  bootbyte_clear();
  EXPECT_EQ(kFallthroughBootAction, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, BootbyteWithCount) {
  // Make sure we ignore the count value in the upper bits.
  bootbyte_set_for_test(0x30 | RTC_BOOT_BOOTLOADER);
  EXPECT_EQ(kBootActionFastboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectA) {
  SetUserInput('1');
  EXPECT_EQ(kBootActionSlotA, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectB) {
  SetUserInput('2');
  EXPECT_EQ(kBootActionSlotB, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectRecovery) {
  SetUserInput('r');
  EXPECT_EQ(kBootActionSlotR, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectFastboot) {
  SetUserInput('f');
  EXPECT_EQ(kBootActionFastboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectNetboot) {
  SetUserInput('n');
  EXPECT_EQ(kBootActionNetboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectNetbootRequiresNetwork) {
  // If user tries to select "n" without a network, we should fall through
  // to whatever the bootloader.default commandline arg has.
  cmdline_set("bootloader.default", "local");
  SetUserInput('n');
  EXPECT_EQ(kBootActionDefault, get_boot_action(false, true));
}

TEST_F(GetBootActionTest, CommandlineLocal) {
  cmdline_set("bootloader.default", "local");
  EXPECT_EQ(kBootActionDefault, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, CommandlineNetwork) {
  cmdline_set("bootloader.default", "network");
  EXPECT_EQ(kBootActionNetboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, CommandlineZedboot) {
  cmdline_set("bootloader.default", "zedboot");
  EXPECT_EQ(kBootActionSlotR, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, CommandlineUnknown) {
  // If "bootloader.default" is an unknown value, default to local.
  cmdline_set("bootloader.default", "foo");
  EXPECT_EQ(kBootActionDefault, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, CommandlineDefault) {
  EXPECT_EQ(kFallthroughBootAction, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, BootbyteFirst) {
  // Make sure the bootbyte is given priority if all are set.
  bootbyte_set_bootloader();
  EXPECT_CALL(mock_input_, ReadKeyStroke).Times(0);
  cmdline_set("bootloader.default", "local");
  EXPECT_EQ(kBootActionFastboot, get_boot_action(true, true));
}

TEST_F(GetBootActionTest, MenuSelectSecond) {
  // Make the user menu is given priority over the commandline.
  SetUserInput('f');
  cmdline_set("bootloader.default", "local");
  EXPECT_EQ(kBootActionFastboot, get_boot_action(true, true));
}

}  // namespace
