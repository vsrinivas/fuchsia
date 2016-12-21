// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGENTA_PLATFORM_CONNECTION_H_
#define _MAGENTA_PLATFORM_CONNECTION_H_

#include "magenta/device/ioctl-wrapper.h"
#include <ddk/ioctl.h>

#define IOCTL_MAGMA_CONNECT IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_GPU, 1)
#define IOCTL_MAGMA_GET_DEVICE_ID IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPU, 2)
#define IOCTL_MAGMA_TEST_RESTART IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_GPU, 3)

#endif // _MAGENTA_PLATFORM_CONNECTION_H_
