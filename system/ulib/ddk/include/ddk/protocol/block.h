// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/ioctl.h>

#define IOCTL_BLOCK_GET_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 1)
#define IOCTL_BLOCK_GET_BLOCKSIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 2)
#define IOCTL_BLOCK_GET_GUID \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 3)
#define IOCTL_BLOCK_GET_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 4)
#define IOCTL_BLOCK_RR_PART \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 5)
