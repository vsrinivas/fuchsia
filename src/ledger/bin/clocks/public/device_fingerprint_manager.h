// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_FINGERPRINT_MANAGER_H_
#define SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_FINGERPRINT_MANAGER_H_

#include <string>

#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace clocks {

class DeviceFingerprintManager {
 public:
  virtual ~DeviceFingerprintManager() = default;

  enum class CloudUploadStatus : bool { UPLOADED, NOT_UPLOADED };

  // Fetches the device fingerprint. Returns |true| if the fingerprint has already been synchronized
  // to the cloud. This is used to detect when the cloud has been erased.
  virtual ledger::Status GetDeviceFingerprint(coroutine::CoroutineHandler* handler,
                                              DeviceFingerprint* device_fingerprint,
                                              CloudUploadStatus* status) = 0;

  // Records that a device fingerprint has been synced with the cloud.
  virtual ledger::Status SetDeviceFingerprintSynced(coroutine::CoroutineHandler* handler) = 0;
};

}  // namespace clocks

#endif  // SRC_LEDGER_BIN_CLOCKS_PUBLIC_DEVICE_FINGERPRINT_MANAGER_H_
