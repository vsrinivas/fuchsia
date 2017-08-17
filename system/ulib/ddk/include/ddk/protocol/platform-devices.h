// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS;

// VID and PID for generic platform devices (that is, devices that may be used by multiple vendors).
#define PDEV_VID_GENERIC    0
#define PDEV_PID_GENERIC    0

// Platform device IDs for generic platform devices
#define PDEV_DID_USB_DWC3   1
#define PDEV_DID_USB_XHCI   2
#define PDEV_DID_KPCI       3

__END_CDECLS;
