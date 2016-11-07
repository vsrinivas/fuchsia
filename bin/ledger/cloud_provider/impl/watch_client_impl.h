// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_WATCH_CLIENT_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_WATCH_CLIENT_IMPL_H_

#include "apps/ledger/src/cloud_provider/public/notification_watcher.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/firebase/watch_client.h"

#include <rapidjson/document.h>

namespace cloud_provider {

// Relay between Firebase and a NotificationWatcher corresponding to
// particular WatchNotifications() request.
class WatchClientImpl : public firebase::WatchClient {
 public:
  WatchClientImpl(firebase::Firebase* firebase,
                  const std::string& firebase_key,
                  const std::string& query,
                  NotificationWatcher* notification_watcher);
  ~WatchClientImpl() override;

  // firebase::WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override;
  void OnError() override;

 private:
  firebase::Firebase* const firebase_;
  NotificationWatcher* const notification_watcher_;
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_WATCH_CLIENT_IMPL_H_
