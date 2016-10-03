// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_
#define APPS_LEDGER_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/ledger/cloud_provider/impl/watch_client_impl.h"
#include "apps/ledger/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/cloud_provider/public/types.h"
#include "apps/ledger/firebase/firebase.h"
#include "apps/ledger/firebase/watch_client.h"

namespace cloud_provider {

class CloudProviderImpl : public CloudProvider {
 public:
  CloudProviderImpl(firebase::Firebase* firebase, const AppId& app_id);
  ~CloudProviderImpl() override;

  // CloudProvider:
  void AddNotification(const PageId& page_id,
                       const Notification& notification,
                       const std::function<void(Status)>& callback) override;

  void WatchNotifications(const PageId& page_id,
                          const std::string& min_timestamp,
                          NotificationWatcher* watcher) override;

  void UnwatchNotifications(NotificationWatcher* watcher) override;

  void GetNotifications(const PageId& page_id,
                        const std::string& min_timestamp,
                        std::function<void(Status, const std::vector<Record>&)>
                            callback) override;

 private:
  // Returns url location where notifications for the particular page are
  // stored.
  std::string GetLocation(const PageId& page_id);

  // Returns the Firebase query filtering the notifications so that only
  // notifications not older than |min_timestamp| are returned. Passing empty
  // |min_timestamp| returns empty query.
  std::string GetTimestampQuery(const std::string& min_timestamp);

  firebase::Firebase* const firebase_;
  const AppId app_id_;
  std::map<NotificationWatcher*, std::unique_ptr<WatchClientImpl>> watchers_;
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_
