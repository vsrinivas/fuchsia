// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#include <stdint.h>

typedef uint32_t usb_mode_t;

// clang-format off
#define USB_MODE_NONE       0
#define USB_MODE_HOST       1
#define USB_MODE_PERIPHERAL 2
#define USB_MODE_OTG        3
// clang-format on
