// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

typedef struct backlight_state {
    bool on;
    uint8_t brightness;
} backlight_state_t;

#define IOCTL_BACKLIGHT_SET_BRIGHTNESS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BACKLIGHT, 1)

#define IOCTL_BACKLIGHT_GET_BRIGHTNESS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BACKLIGHT, 2)

IOCTL_WRAPPER_IN(ioctl_backlight_set_state, IOCTL_BACKLIGHT_SET_BRIGHTNESS, backlight_state_t);

IOCTL_WRAPPER_OUT(ioctl_backlight_get_state, IOCTL_BACKLIGHT_GET_BRIGHTNESS, backlight_state_t);