// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENVIRONMENT_NOTIFICATION_H_
#define SRC_LEDGER_BIN_ENVIRONMENT_NOTIFICATION_H_

namespace ledger {

// A `Notification` allows threads to receive notification of a single
// occurrence of a single event.
class Notification {
 public:
  virtual ~Notification() = default;

  // Returns `true` if `Notify()` has been called on this notification.
  virtual bool HasBeenNotified() const = 0;

  // Blocks until `Notify` is called on this notification. If `Notify` has already been called,
  // return immediately.
  virtual void WaitForNotification() const = 0;

  // Wakes up threads currently blocked in `WaitForNotification`, if any. Also registers the call to
  // avoid blocking future calls to `WaitForNotification`. Must only be called once.
  virtual void Notify() = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_ENVIRONMENT_NOTIFICATION_H_
