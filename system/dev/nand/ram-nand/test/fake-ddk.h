// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>

namespace fake_ddk {

// Fake instances of a parent device, and device returned by DeviceAdd.
extern zx_device_t* kFakeDevice;
extern zx_device_t* kFakeParent;

// Mocks the bind/unbind functionality provided by the DDK(TL).
//
// The typical use of this class is something like:
//      fake_ddk::Bind ddk;
//      device->Bind();
//      device->DdkUnbind();
//      EXPECT_TRUE(ddk.Ok());
//
// Note that this class is not thread safe. Only one test at a time is supported.
class Bind {
  public:
    Bind();
    ~Bind() { instance_ = nullptr; }

    static Bind* Instance() { return instance_; }

    // Internal fake implementation of ddk functionality.
    zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent,
                          device_add_args_t* args, zx_device_t** out);

    // Internal fake implementation of ddk functionality.
    zx_status_t DeviceRemove(zx_device_t* device);

    // Verifies that the whole process of bind and unbind went as expected.
    bool Ok();

    static Bind* instance_;

  private:
    bool bad_parent_ = false;
    bool bad_device_ = false;
    bool add_called_ = false;
    bool remove_called_ = false;
};

}  // namespace fake_ddk
