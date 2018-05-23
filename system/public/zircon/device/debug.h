// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

#define DEBUG_STREAM_ID_RESERVED 0

// Sets the device instance stream ID for reading and writing from. The ID must be non-zero.
// call with in_len = sizeof(uint32_t)
#define IOCTL_DEBUG_SET_STREAM IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEBUG, 0)
IOCTL_WRAPPER_IN(ioctl_debug_set_stream_id, IOCTL_DEBUG_SET_STREAM, uint32_t);
