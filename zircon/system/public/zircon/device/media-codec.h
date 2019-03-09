// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_MEDIA_CODEC_H_
#define SYSROOT_ZIRCON_DEVICE_MEDIA_CODEC_H_

#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

#define MEDIA_CODEC_IOCTL_GET_CODEC_FACTORY_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, 0xFE, 0x00)
IOCTL_WRAPPER_OUT(
    ioctl_media_codec_get_codec_factory_channel,
    MEDIA_CODEC_IOCTL_GET_CODEC_FACTORY_CHANNEL,
    zx_handle_t);

#endif  // SYSROOT_ZIRCON_DEVICE_MEDIA_CODEC_H_
