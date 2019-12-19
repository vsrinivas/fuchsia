// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_SUSPEND_TXN_H_
#define DDK_SUSPEND_TXN_H_

#include <stdio.h>
#include <zircon/assert.h>

#include <ddk/device.h>
#include <ddk/driver.h>

namespace ddk {

class SuspendTxn {
 public:
  explicit SuspendTxn(zx_device_t* dev, uint8_t requested_state, bool enable_wake,
                      uint8_t suspend_reason)
      : dev_(dev),
        requested_state_(requested_state),
        suspend_reason_(suspend_reason),
        enable_wake_(enable_wake) {}

  ~SuspendTxn() {
    if (dev_) {
      ZX_ASSERT_MSG(replied_, "SuspendTxn must have it's Reply() method used.\n");
    }
  }

  SuspendTxn(SuspendTxn&& other) { MoveHelper(other); }
  SuspendTxn& operator=(SuspendTxn&& other) {
    MoveHelper(other);
    return *this;
  }

  // This is used to signify the completion of the device's suspend() hook.
  // It does not necessarily need to be called from within the suspend() hook.
  void Reply(zx_status_t status, uint8_t out_state) {
    ZX_ASSERT_MSG(dev_, "SuspendTxn did not contain any device pointer.\n");
    ZX_ASSERT_MSG(!replied_, "Cannot reply to SuspendTxn twice.");
    replied_ = true;
    device_suspend_reply(dev_, status, out_state);
  }

  uint8_t requested_state() { return requested_state_; }
  uint8_t suspend_reason() { return suspend_reason_; }
  bool enable_wake() { return enable_wake_; }

 private:
  zx_device_t* dev_ = nullptr;
  bool replied_ = false;
  uint8_t requested_state_;
  uint8_t suspend_reason_;
  bool enable_wake_;
  // Move-only type
  void MoveHelper(SuspendTxn& other) {
    dev_ = other.dev_;
    replied_ = other.replied_;
    requested_state_ = other.requested_state_;
    suspend_reason_ = other.suspend_reason_;
    enable_wake_ = other.enable_wake_;
    other.dev_ = nullptr;
    other.replied_ = false;
    other.requested_state_ = 0;
    other.suspend_reason_ = 0;
    other.enable_wake_ = false;
  }
};

}  // namespace ddk

#endif  // DDK_SUSPEND_TXN_H_
