// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_INIT_TXN_H_
#define DDK_INIT_TXN_H_

#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/assert.h>

namespace ddk {

class InitTxn {
 public:
  explicit InitTxn(zx_device_t* dev) : dev_(dev) {}

  ~InitTxn() {
    if (dev_) {
      ZX_ASSERT_MSG(replied_, "InitTxn must have it's Reply() method used.\n");
    }
  }

  InitTxn(InitTxn&& other) { MoveHelper(other); }
  InitTxn& operator=(InitTxn&& other) {
    MoveHelper(other);
    return *this;
  }

  // This is used to signify the completion of the device's Init() hook.
  // It does not necessarily need to be called from within the Init() hook.
  void Reply(zx_status_t status,
             const device_power_state_info_t* power_states = nullptr,
             const uint8_t power_state_count = 0,
             const device_performance_state_info_t* perf_power_states = nullptr,
             const uint8_t perf_power_state_count = 0) {
    device_init_reply_args_t args = {};
    args.power_states = power_states;
    args.power_state_count = power_state_count;
    args.performance_states = perf_power_states;
    args.performance_state_count = perf_power_state_count;

    ZX_ASSERT_MSG(dev_, "InitTxn did not contain any device pointer.\n");
    ZX_ASSERT_MSG(!replied_, "Cannot reply to InitTxn twice.");
    replied_ = true;
    device_init_reply(dev_, status, &args);
  }

 private:
  // Move-only type
  void MoveHelper(InitTxn& other) {
    dev_ = other.dev_;
    replied_ = other.replied_;
    other.dev_ = nullptr;
    other.replied_ = false;
  }

  zx_device_t* dev_ = nullptr;
  bool replied_ = false;
};

}  // namespace ddk

#endif  // DDK_INIT_TXN_H_
