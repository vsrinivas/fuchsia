// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/ioctl.h>

#define IOCTL_DEVICE_BIND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 0)
