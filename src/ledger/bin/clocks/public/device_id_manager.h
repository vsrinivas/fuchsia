// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_ID_MANAGER_H_
#define SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_ID_MANAGER_H_

#include <string>

#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace clocks {

class DeviceIdManager {
 public:
  virtual ~DeviceIdManager() = default;

  // Informs the DeviceIdManager that a page has been deleted.
  virtual ledger::Status OnPageDeleted(coroutine::CoroutineHandler* handler) = 0;

  // Returns a new DeviceId to be used in a clock entry by this device.
  virtual ledger::Status GetNewDeviceId(coroutine::CoroutineHandler* handler,
                                        DeviceId* device_id) = 0;
};

}  // namespace clocks

#endif  // SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_ID_MANAGER_H_
