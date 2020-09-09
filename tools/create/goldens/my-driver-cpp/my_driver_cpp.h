// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CREATE_GOLDENS_MY_DRIVER_CPP_MY_DRIVER_CPP_H_
#define TOOLS_CREATE_GOLDENS_MY_DRIVER_CPP_MY_DRIVER_CPP_H_

#include <ddktl/device.h>

namespace my_driver_cpp {

class MyDriverCpp;
using DeviceType = ddk::Device<MyDriverCpp, ddk::Initializable, ddk::Unbindable>;
class MyDriverCpp : public DeviceType {
 public:
  explicit MyDriverCpp(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~MyDriverCpp() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
};

}  // namespace my_driver_cpp

#endif  // TOOLS_CREATE_GOLDENS_MY_DRIVER_CPP_MY_DRIVER_CPP_H_
