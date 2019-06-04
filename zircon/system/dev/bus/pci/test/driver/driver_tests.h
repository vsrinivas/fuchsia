// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the driver protocol tests.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_DRIVER_DRIVER_TESTS_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_DRIVER_DRIVER_TESTS_H_

#include <inttypes.h>

#define PCI_TEST_DRIVER_VID 0x0eff
#define PCI_TEST_DRIVER_DID 0x0fff

#define PCI_TEST_BUS_ID 0x00
#define PCI_TEST_DEV_ID 0x01
#define PCI_TEST_FUNC_ID 0x02

constexpr char kFakeBusDriverName[] = "pcictl";
constexpr char kProtocolTestDriverName[] = "pciproto";


#endif  // ZIRCON_SYSTEM_DEV_BUS_PCI_TEST_DRIVER_DRIVER_TESTS_H_
