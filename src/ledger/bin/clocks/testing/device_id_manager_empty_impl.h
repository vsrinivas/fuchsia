// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOCKS_TESTING_DEVICE_ID_MANAGER_EMPTY_IMPL_H_
#define SRC_LEDGER_BIN_CLOCKS_TESTING_DEVICE_ID_MANAGER_EMPTY_IMPL_H_

#include "src/ledger/bin/clocks/public/device_id_manager.h"

namespace clocks {

class DeviceIdManagerEmptyImpl : public DeviceIdManager {
 public:
  DeviceIdManagerEmptyImpl();
  ~DeviceIdManagerEmptyImpl() override;

  // DeviceIdManager:
  ledger::Status OnPageDeleted(coroutine::CoroutineHandler* handler) override;
  ledger::Status GetNewDeviceId(coroutine::CoroutineHandler* handler, DeviceId* device_id) override;
};

}  // namespace clocks

#endif  // SRC_LEDGER_BIN_CLOCKS_TESTING_DEVICE_ID_MANAGER_EMPTY_IMPL_H_
