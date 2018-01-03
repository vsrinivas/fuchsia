// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <fbl/type_support.h>
#include <zircon/assert.h>

// DDK bt-hci protocol support
//
// :: Mixins ::
//
// ddk::BtHciProtocol enables writing DDK drivers that implement the bt-hci
// protocol.  The bt-hci protocol is currently empty, being a protocol that is
// implemented entirely through ioctls.
//
// :: Example ::
//
// // A driver that implements a ZX_PROTOCOL_BT_HCI device
// class BtHciDevice;
// using BtHciDeviceType = ddk::Device<BtHciDevice, ddk::Ioctlable,
//                                    /* other ddk mixins */>;
//
// class BtHciDevice : public BtHciDeviceType,
//                     public ddk::BtHciProtocol<BtHciDevice> {
//   public:
//     BtHciDevice(zx_device_t* parent)
//       : BtHciDeviceType(parent)  {}
//
//     zx_status_t Bind() {
//         return DdkAdd("my-bt-hci-device");
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     zx_status_t DdkIoctl(uint32_t op, const void *in_buf ... ) {
//        // Handle IOCTL_BT_HCI_*
//     }
// };
//

namespace ddk {

template <typename D>
class BtHciProtocol : public internal::base_protocol {
public:
    BtHciProtocol() {
        // Can only inherit from one base_protocol implementation
        ZX_ASSERT(this->ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_BT_HCI;
    }
};

}  // namespace ddk
