// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/create/goldens/my-driver-cpp/my_driver_cpp.h"

#include "tools/create/goldens/my-driver-cpp/my_driver_cpp-bind.h"

namespace my_driver_cpp {

zx_status_t MyDriverCpp::Bind(void* ctx, zx_device_t* dev) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t MyDriverCpp::Bind() { return DdkAdd("my_driver_cpp"); }

void MyDriverCpp::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void MyDriverCpp::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void MyDriverCpp::DdkRelease() { delete this; }

static zx_driver_ops_t my_driver_cpp_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = MyDriverCpp::Bind;
  return ops;
}();

}  // namespace my_driver_cpp

ZIRCON_DRIVER(MyDriverCpp, my_driver_cpp::my_driver_cpp_driver_ops, "zircon", "0.1")
