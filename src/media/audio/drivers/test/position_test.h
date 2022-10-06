// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_POSITION_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_POSITION_TEST_H_

#include "src/media/audio/drivers/test/admin_test.h"

namespace media::audio::drivers::test {

class PositionTest : public AdminTest {
 public:
  explicit PositionTest(const DeviceEntry& dev_entry) : AdminTest(dev_entry) {}

 protected:
  // Request a position notification that will record timestamp/position and register for another.
  void EnablePositionNotifications();
  // Clear flag so that any pending position notification will not request yet another.
  void DisablePositionNotifications() { request_next_position_notification_ = false; }
  void DisallowPositionNotifications() { position_notification_is_expected_ = false; }

  void RequestPositionNotification();
  void PositionNotificationCallback(fuchsia::hardware::audio::RingBufferPositionInfo position_info);
  void ExpectPositionNotifyCount(uint32_t count);
  void ValidatePositionInfo();

 private:
  fuchsia::hardware::audio::RingBufferPositionInfo saved_position_ = {};

  // Watching for position info is a hanging-get. On receipt, this flag determines whether we
  // register for the next notification.
  bool request_next_position_notification_ = false;
  bool position_notification_is_expected_ = true;
  bool record_position_info_ = false;
  uint32_t position_notification_count_ = 0;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_POSITION_TEST_H_
