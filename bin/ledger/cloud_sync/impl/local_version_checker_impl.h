// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_

#include <functional>
#include <string>

#include "apps/ledger/src/callback/destruction_sentinel.h"
#include "apps/ledger/src/cloud_sync/public/local_version_checker.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/firebase/watch_client.h"

#include <rapidjson/document.h>

namespace cloud_sync {

class LocalVersionCheckerImpl : public LocalVersionChecker,
                                public firebase::WatchClient {
 public:
  LocalVersionCheckerImpl(std::unique_ptr<firebase::Firebase> user_firebase);
  ~LocalVersionCheckerImpl() override;

  void CheckFingerprint(std::string auth_token,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override;

  void SetFingerprint(std::string auth_token,
                      std::string fingerprint,
                      std::function<void(Status)> callback) override;

  void WatchFingerprint(std::string auth_token,
                        std::string fingerprint,
                        std::function<void(Status)> callback) override;

  // firebase::WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override;
  void OnPatch(const std::string& path, const rapidjson::Value& value) override;
  void OnCancel() override;
  void OnAuthRevoked(const std::string& reason) override;
  void OnMalformedEvent() override;
  void OnConnectionError() override;

 private:
  void ResetWatcher();
  std::unique_ptr<firebase::Firebase> user_firebase_;
  bool firebase_watcher_set_ = false;
  std::function<void(Status)> watch_callback_;

  callback::DestructionSentinel destruction_sentinel_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_LOCAL_VERSION_CHECKER_IMPL_H_
