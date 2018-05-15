// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

// DDK audio-codec protocol support
//
// :: Mixins ::
//
// ddk::AudioCodecProtocol is a mixin class that simplifies writing DDK drivers that
// interact with the audio-codec protocol. It takes care of implementing the function
// pointer tables and calling into the object that wraps it.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_AUDIO_CODEC device
// class AudioCodecDevice;
// using AudioCodecDeviceType = ddk::Device<AudioCodecDevice, /* ddk mixins */>;
//
// class AudioCodecDevice : public AudioCodecDeviceType,
//                          public ddk::AudioCodecProtocol<AudioCodecDevice> {
//   public:
//     AudioCodecDevice(zx_device_t* parent)
//       : AudioCodecDeviceType("my-audio-codec-device", parent) {}
//
//     zx_status_t Bind() {
//         DdkAdd();
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     ...
//   private:
//     ...
// };

namespace ddk {

template <typename D>
class AudioCodecProtocol : public internal::base_protocol {
  public:
    AudioCodecProtocol() {
        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_AUDIO_CODEC;
        ddk_proto_ops_ = &ops_;
    }

  private:
    // Empty struct to use for ops, so that we do not break the invariant that ddk_proto_ops_ is
    // non-null for devices with a protocol.
    struct {
    } ops_;
};

}  // namespace ddk
