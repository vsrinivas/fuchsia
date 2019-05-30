// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <wlan/common/dispatcher.h>

#include <fuchsia/wlan/device/cpp/fidl.h>

namespace wlanphy {

class Device : public ::fuchsia::wlan::device::Phy {
   public:
    Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto);
    ~Device();

    zx_status_t Bind();

    // zx_protocol_device_t
    zx_status_t Message(fidl_msg_t* msg, fidl_txn_t* txn);
    void Release();
    void Unbind();

    // wlanphy_protocol_t from ::fuchsia::wlan::device::Phy
    virtual void Query(QueryCallback callback) override;
    virtual void CreateIface(::fuchsia::wlan::device::CreateIfaceRequest req,
                             CreateIfaceCallback callback) override;
    virtual void DestroyIface(::fuchsia::wlan::device::DestroyIfaceRequest req,
                              DestroyIfaceCallback callback) override;

   private:
    zx_status_t Connect(zx::channel request);

    zx_device_t* parent_;
    zx_device_t* zxdev_;

    wlanphy_impl_protocol_t wlanphy_impl_;

    wlan::common::Dispatcher<::fuchsia::wlan::device::Phy> dispatcher_;
};

}  // namespace wlanphy
