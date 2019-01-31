// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// Enables/Disables the audio codec.
#define IOCTL_AUDIO_CODEC_ENABLE IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO_CODEC, 1)

// ssize_t ioctl_audio_codec_enable(int fd, bool enable)
IOCTL_WRAPPER_IN(ioctl_audio_codec_enable, IOCTL_AUDIO_CODEC_ENABLE, bool);
