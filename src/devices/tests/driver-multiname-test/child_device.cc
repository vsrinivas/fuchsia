// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/driver-multiname-test/child_device.h"

namespace child_device {

void ChildDevice::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void ChildDevice::DdkRelease() { delete this; }

}  // namespace child_device
