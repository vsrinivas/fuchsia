// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <magenta/compiler.h>
#include <stddef.h>
#include <sys/types.h>

__BEGIN_CDECLS;

#define IOCTL_BCM_POWER_ON_USB \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 0)

__END_CDECLS
