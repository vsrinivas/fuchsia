// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace usb {

class FlashProgrammer;
using FlashProgrammerBase = ddk::Device<FlashProgrammer, ddk::Messageable, ddk::Unbindable>;

class FlashProgrammer : public FlashProgrammerBase,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_USB_TEST_FWLOADER> {

public:
    // Spawns device node based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

private:
    explicit FlashProgrammer(zx_device_t* parent) : FlashProgrammerBase(parent) {}

    zx_status_t Bind();
};

}  // namespace usb