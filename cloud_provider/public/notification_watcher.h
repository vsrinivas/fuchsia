// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_WATCHER_H_
#define APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_WATCHER_H_

#include "apps/ledger/cloud_provider/public/notification.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

class NotificationWatcher {
 public:
  NotificationWatcher() {}
  virtual ~NotificationWatcher() {}

  // Called when a new notification is added to the cloud. |timestamp| is opaque
  // to the client, but can be passed back to CloudProvider as a query
  // parameter.
  virtual void OnNewNotification(const Notification& notification,
                                 const std::string& timestamp) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(NotificationWatcher);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_WATCHER_H_
