// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_FIDL_LLCPP_DRIVER_DRIVER_H_
#define ZIRCON_SYSTEM_DEV_FIDL_LLCPP_DRIVER_DRIVER_H_

#include <ddk/driver.h>
#include <ddktl/fidl.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <fuchsia/hardware/serial/llcpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace fidl {

class DdkFidlDevice;
using DeviceType = ddk::Device<DdkFidlDevice,
                               ddk::Messageable>;

class DdkFidlDevice : public DeviceType,
                      public ::llcpp::fuchsia::hardware::serial::Device::Interface {
public:
    explicit DdkFidlDevice(zx_device_t* parent)
        : DeviceType(parent) {}

    static zx_status_t Create(void* ctx, zx_device_t* dev);
    zx_status_t Bind();

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkRelease();

    // LLCPP Fidl protocols
    void GetClass(GetClassCompleter::Sync completer) override;
    void SetConfig(::llcpp::fuchsia::hardware::serial::Config config, SetConfigCompleter::Sync completer) override;
};
} // namespace fidl

#endif  // ZIRCON_SYSTEM_DEV_FIDL_LLCPP_DRIVER_DRIVER_H_
