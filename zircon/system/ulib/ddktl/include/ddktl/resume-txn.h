// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_RESUME_TXN_H_
#define DDK_RESUME_TXN_H_

#include <stdio.h>
#include <zircon/assert.h>

#include <ddk/device.h>
#include <ddk/driver.h>

namespace ddk {

class ResumeTxn {
 public:
  explicit ResumeTxn(zx_device_t* dev, uint32_t requested_state)
      : dev_(dev), requested_state_(requested_state) {}
  ~ResumeTxn() {
    if (dev_) {
      ZX_ASSERT_MSG(replied_, "ResumeTxn must have it's Reply() method used.\n");
    }
  }

  ResumeTxn(ResumeTxn&& other) { MoveHelper(other); }
  ResumeTxn& operator=(ResumeTxn&& other) {
    MoveHelper(other);
    return *this;
  }

  // This is used to signify the completion of the device's resume() hook.
  // It does not necessarily need to be called from within the resume() hook.
  void Reply(zx_status_t status, uint8_t out_power_state, uint32_t out_performance_state) {
    ZX_ASSERT_MSG(dev_, "ResumeTxn did not contain any device pointer.\n");
    ZX_ASSERT_MSG(!replied_, "Cannot reply to ResumeTxn twice.");
    replied_ = true;
    device_resume_reply(dev_, status, out_power_state, out_performance_state);
  }

  uint32_t requested_state() { return requested_state_; }

 private:
  zx_device_t* dev_ = nullptr;
  bool replied_ = false;
  uint32_t requested_state_;
  // Move-only type
  void MoveHelper(ResumeTxn& other) {
    dev_ = other.dev_;
    replied_ = other.replied_;
    requested_state_ = other.requested_state_;
    other.dev_ = nullptr;
    other.replied_ = false;
    other.requested_state_ = 0;
  }
};

}  // namespace ddk

#endif  // DDK_RESUME_TXN_H_
