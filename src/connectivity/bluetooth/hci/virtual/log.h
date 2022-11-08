// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOG_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOG_H_

#define logf(flag, fmt...) zxlogf(flag, "bt-hci-emulator: " fmt)

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOG_H_
