// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>

#define IOCTL_DEVMGR_MOUNT_FS \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVMGR, 0)

