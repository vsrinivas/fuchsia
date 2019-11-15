// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOCKS_IMPL_DEVICE_ID_MANAGER_IMPL_H_
#define SRC_LEDGER_BIN_CLOCKS_IMPL_DEVICE_ID_MANAGER_IMPL_H_

#include <string>

#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/clocks/public/device_id_manager.h"
#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/synchronization/completer.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace clocks {

class DeviceIdManagerImpl : public DeviceIdManager, public DeviceFingerprintManager {
 public:
  DeviceIdManagerImpl(ledger::Environment* environment, std::unique_ptr<storage::Db> db);
  ~DeviceIdManagerImpl() override;

  // Asynchronously initializes this DeviceIdManager.
  ledger::Status Init(coroutine::CoroutineHandler* handler);

  // DeviceFingerprintManager:
  ledger::Status GetDeviceFingerprint(coroutine::CoroutineHandler* handler,
                                      DeviceFingerprint* device_fingerprint,
                                      CloudUploadStatus* status) override;
  ledger::Status SetDeviceFingerprintSynced(coroutine::CoroutineHandler* handler) override;
  // DeviceIdManager:
  ledger::Status OnPageDeleted(coroutine::CoroutineHandler* handler) override;
  ledger::Status GetNewDeviceId(coroutine::CoroutineHandler* handler, DeviceId* device_id) override;

 private:
  // Private helper method for the public Init() method.
  ledger::Status InternalInit(coroutine::CoroutineHandler* handler);

  ledger::Environment* const environment_;
  ledger::Completer initialization_completer_;

  std::unique_ptr<storage::Db> db_;
  DeviceFingerprint fingerprint_;
  CloudUploadStatus upload_status_;
  uint64_t counter_;
};

}  // namespace clocks

#endif  // SRC_LEDGER_BIN_CLOCKS_IMPL_DEVICE_ID_MANAGER_IMPL_H_
