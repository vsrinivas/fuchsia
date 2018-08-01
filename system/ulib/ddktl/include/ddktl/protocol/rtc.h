// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

// DDK RTC protocol support
//
// :: Mixins ::
//
// ddk::RTCProtocol enables writing DDK drivers that implement the RTC
// protocol.  The RTC protocol is currently empty, being a protocol that is
// implemented entirely through ioctls.
//
// :: Example ::
//
// // A driver that implements a ZX_PROTOCOL_RTC device
// class MyRtcDevice;
// using MyRtcDeviceType = ddk::Device<MyRtcDevice, ddk::Ioctlable,
//                                    /* other ddk mixins */>;
//
// class MyRtcDevice : public MyRtcDeviceType,
//                     public ddk::RTCProtocol<MyRtcDevice> {
//   public:
//     MyRtcDevice(zx_device_t* parent)
//       : MyRtcDeviceType(parent)  {}
//
//     zx_status_t Bind() {
//         return DdkAdd("my-rtc-device");
//     }
//
//     void DdkRelease() {
//         // Clean up.
//     }
//
//     zx_status_t DdkIoctl(uint32_t op, const void *in_buf ... ) {
//        // Handle IOCTL_RTC_* operations.
//     }
// };
//

namespace ddk {

template <typename D>
class RtcProtocol : public internal::base_protocol {
public:
    RtcProtocol() {
        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(this->ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_RTC;
    }
};

}  // namespace ddk
