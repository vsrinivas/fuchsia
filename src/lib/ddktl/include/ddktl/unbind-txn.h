// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_UNBIND_TXN_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_UNBIND_TXN_H_

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>

namespace ddk {

class UnbindTxn {
 public:
  explicit UnbindTxn(zx_device_t* dev) : dev_(dev) {}

  ~UnbindTxn() {
    if (dev_) {
      ZX_ASSERT_MSG(replied_, "UnbindTxn must have it's Reply() method used.\n");
    }
  }

  // Move-only type
  void MoveHelper(UnbindTxn& other) {
    dev_ = other.dev_;
    replied_ = other.replied_;
    other.dev_ = nullptr;
    other.replied_ = false;
  }

  UnbindTxn(UnbindTxn&& other) { MoveHelper(other); }
  UnbindTxn& operator=(UnbindTxn&& other) {
    MoveHelper(other);
    return *this;
  }

  // This is used to signify the completion of the device's unbind() hook.
  // It does not necessarily need to be called from within the unbind() hook.
  void Reply() {
    ZX_ASSERT_MSG(dev_, "UnbindTxn did not contain any device pointer.\n");
    ZX_ASSERT_MSG(!replied_, "Cannot reply to UnbindTxn twice.");
    replied_ = true;
    device_unbind_reply(dev_);
  }

 private:
  zx_device_t* dev_ = nullptr;
  bool replied_ = false;
};

}  // namespace ddk

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_UNBIND_TXN_H_
