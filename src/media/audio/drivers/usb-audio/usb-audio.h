// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_H_

#include <lib/ddk/device.h>
#include <zircon/compiler.h>

#include <fbl/array.h>
#include <usb/audio.h>
#include <usb/usb.h>

namespace audio {
namespace usb {

// clang-format off
enum class Direction { Unknown, Input, Output };
enum class EndpointSyncType : uint8_t {
    None     = USB_ENDPOINT_NO_SYNCHRONIZATION,
    Async    = USB_ENDPOINT_ASYNCHRONOUS,
    Adaptive = USB_ENDPOINT_ADAPTIVE,
    Sync     = USB_ENDPOINT_SYNCHRONOUS,
};
// clang-format on

fbl::Array<uint8_t> FetchStringDescriptor(const usb_protocol_t& usb, uint8_t desc_id,
                                          uint16_t lang_id = 0, uint16_t* out_lang_id = nullptr);

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_H_
