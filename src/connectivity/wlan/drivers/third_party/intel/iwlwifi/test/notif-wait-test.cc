// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is to test the fw/notif-wait.c file.
//
// In the design concept of notif-wait.c, there are 2 parties invoked: caller (usually the firmware
// IRQ) and the user (the driver code waiting for the notification from firmware). In this file,
// we will act as those 2 parties in a test case.

#include <iterator>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/notif-wait.h"
}

namespace wlan {
namespace testing {
namespace {

enum FakeCommandId : uint16_t {
  FAKE_CMD_1 = 1,
  FAKE_CMD_2,
};

#define FAKE_PKT_1         \
  {                        \
    .hdr = {               \
        .cmd = FAKE_CMD_1, \
        .group_id = 0,     \
    },                     \
  }

#define FAKE_PKT_2         \
  {                        \
    .hdr = {               \
        .cmd = FAKE_CMD_2, \
        .group_id = 0,     \
    },                     \
  }

// Helper function to create a wait_data and a wait_entry to receive the fake cmd.
static void helper_create_fake_cmd(struct iwl_notif_wait_data* wait_data,
                                   struct iwl_notification_wait* wait_entry, uint16_t fake_cmd) {
  uint16_t cmds[] = {fake_cmd};
  iwl_init_notification_wait(wait_data, wait_entry, cmds, std::size(cmds), nullptr, nullptr);
}

// Helper function for test case init. This would create a wait_entry for fake cmd 1.
static void helper_test_case_init(struct iwl_notif_wait_data* wait_data,
                                  struct iwl_notification_wait* wait_entry) {
  iwl_notification_wait_init(wait_data);
  helper_create_fake_cmd(wait_data, wait_entry, FAKE_CMD_1);
}

class NotifWaitTest : public ::zxtest::Test {
 public:
  NotifWaitTest() {
    helper_test_case_init(&wait_data_, &wait_entry_);
  }
  ~NotifWaitTest() { mtx_destroy(&wait_data_.notif_wait_lock); }

 protected:
  struct iwl_notif_wait_data wait_data_;     // A global wait data
  struct iwl_notification_wait wait_entry_;  // A local wait entry used to wait for a specific event
};

// The normal case. Expect we can get the notification.
TEST_F(NotifWaitTest, NormalCase) {
  // Here comes a packet. This will mark the notification has been triggered.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  iwl_notification_wait_notify(&wait_data_, &pkt);

  // Wait for the entry and expect success,
  EXPECT_EQ(ZX_OK, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

// Without a trigger, the second wait should time out.
TEST_F(NotifWaitTest, SecondWaitWithoutTriggerShouldTimeOut) {
  // The first wait. Should be successful.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  iwl_notification_wait_notify(&wait_data_, &pkt);
  EXPECT_EQ(ZX_OK, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));

  // The second wait should time out because it is not triggered.
  helper_create_fake_cmd(&wait_data_, &wait_entry_, FAKE_CMD_1);
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

// Test 2 rounds of trigger/wait (same pkt). Expect both should be successful.
TEST_F(NotifWaitTest, CanBeTriggeredAgain) {
  // The first wait. Should be successful.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  iwl_notification_wait_notify(&wait_data_, &pkt);
  EXPECT_EQ(ZX_OK, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));

  // The second wait. Should be successful as well.
  helper_create_fake_cmd(&wait_data_, &wait_entry_, FAKE_CMD_1);
  iwl_notification_wait_notify(&wait_data_, &pkt);
  EXPECT_EQ(ZX_OK, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

// We expect one command, but only other command is notified.
TEST_F(NotifWaitTest, TestNotInterestedCommand) {
  // Here comes a packet. But not what we are interested. Expect it will be ignored.
  struct iwl_rx_packet pkt = FAKE_PKT_2;
  iwl_notification_wait_notify(&wait_data_, &pkt);

  // Wait for the notification, but expect timeout.
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

// Expect the waiting is aborted.
TEST_F(NotifWaitTest, TestAbortion) {
  // Abort all.
  iwl_abort_notification_waits(&wait_data_);

  // Expect aborted.
  EXPECT_EQ(ZX_ERR_CANCELED, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

// Try to trigger it first, then abort it.
TEST_F(NotifWaitTest, TestTriggeredThenAborted) {
  // Trigger
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  iwl_notification_wait_notify(&wait_data_, &pkt);

  // Abort
  iwl_abort_notification_waits(&wait_data_);

  // Expect aborted.
  EXPECT_EQ(ZX_ERR_CANCELED, iwl_wait_notification(&wait_data_, &wait_entry_, ZX_TIME_INFINITE_PAST));
}

class NotificationWaitTest : public ::zxtest::Test {
 public:
  NotificationWaitTest() {}
  ~NotificationWaitTest() {}
};

//////////////////////////////////////////////////////////////////////////////////////////////////
//
// Below tests only focus on iwl_notification_wait().

TEST_F(NotificationWaitTest, TestEmptyWaitList) {
  // A global wait data.
  struct iwl_notif_wait_data wait_data;
  iwl_notification_wait_init(&wait_data);

  // Nothing has been added into the wait list.

  struct iwl_rx_packet pkt = FAKE_PKT_1;
  EXPECT_FALSE(iwl_notification_wait(&wait_data, &pkt));
  mtx_destroy(&wait_data.notif_wait_lock);
}

TEST_F(NotifWaitTest, TestAbortedThenTriggered) {
  // Abort
  iwl_abort_notification_waits(&wait_data_);

  // Trigger it and expect we cannot find it.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  EXPECT_FALSE(iwl_notification_wait(&wait_data_, &pkt));
}

TEST_F(NotifWaitTest, TestRemove) {
  // Remove it
  iwl_remove_notification(&wait_data_, &wait_entry_);

  // Trigger it and expect we cannot find it.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  EXPECT_FALSE(iwl_notification_wait(&wait_data_, &pkt));
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//
// Test 'fn' -- the callback function.

// A callback function that returns:
//
//   1. true when 'data' is not nullptr. Will also mark 'data' to true.
//   2. otherwise, false.
//
static bool fn(struct iwl_notif_wait_data* notif_wait, struct iwl_rx_packet* pkt, void* data) {
  if (data) {
    *reinterpret_cast<bool*>(data) = true;
    return true;
  } else {
    return false;
  }
}

// Helper function to create a wait_data, a wait_entry, and setup a callback function pointer.
static void helper_create_fn(struct iwl_notif_wait_data* wait_data,
                             struct iwl_notification_wait* wait_entry,
                             bool (*fn)(struct iwl_notif_wait_data* notif_data,
                                        struct iwl_rx_packet* pkt, void* data),
                             void* fn_data) {
  iwl_notification_wait_init(wait_data);

  uint16_t cmds[] = {FAKE_CMD_1};
  iwl_init_notification_wait(wait_data, wait_entry, cmds, std::size(cmds), fn, fn_data);
}

TEST_F(NotificationWaitTest, FnReturnsTrue) {
  struct iwl_notif_wait_data wait_data;     // A global wait data.
  struct iwl_notification_wait wait_entry;  // A local wait entry used to wait for a specific event
  bool fn_data = false;
  helper_create_fn(&wait_data, &wait_entry, fn, &fn_data);

  // Trigger it. Expect the trigger is valid and 'fn_data' has been marked.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  EXPECT_TRUE(iwl_notification_wait(&wait_data, &pkt));
  EXPECT_TRUE(fn_data);
  mtx_destroy(&wait_data.notif_wait_lock);
}

TEST_F(NotificationWaitTest, FnReturnsFalse) {
  struct iwl_notif_wait_data wait_data;     // A global wait data.
  struct iwl_notification_wait wait_entry;  // A local wait entry used to wait for a specific event
  bool fn_data = false;
  helper_create_fn(&wait_data, &wait_entry, fn, nullptr);

  // Trigger it. Expect the trigger is invalid and 'fn_data' is unchanged.
  struct iwl_rx_packet pkt = FAKE_PKT_1;
  EXPECT_FALSE(iwl_notification_wait(&wait_data, &pkt));
  EXPECT_FALSE(fn_data);
  mtx_destroy(&wait_data.notif_wait_lock);
}

}  // namespace
}  // namespace testing
}  // namespace wlan
