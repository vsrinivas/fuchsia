// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/usb/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/usb-dfu.h>

namespace usb {

class Dfu;
using DfuBase = ddk::Device<Dfu, ddk::Messageable, ddk::Unbindable>;

class Dfu : public DfuBase,
            public ddk::EmptyProtocol<ZX_PROTOCOL_USB_TEST_FWLOADER> {

public:
    Dfu(zx_device_t* parent, uint8_t intf_num, const usb_dfu_func_desc_t& func_desc)
        : DfuBase(parent),
          intf_num_(intf_num),
          func_desc_(func_desc) {}

    // Spawns device node based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

private:
    zx_status_t Bind();

    const uint8_t intf_num_;
    const usb_dfu_func_desc_t func_desc_;
};

}  // namespace usb
