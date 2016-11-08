// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_

#include <functional>
#include <string>
#include <vector>

#include "apps/ledger/src/cloud_provider/public/notification.h"
#include "apps/ledger/src/cloud_provider/public/notification_watcher.h"
#include "apps/ledger/src/cloud_provider/public/record.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "lib/ftl/macros.h"
#include "mx/datapipe.h"
#include "mx/vmo.h"

namespace cloud_provider {

// This API captures Ledger requirements for a cloud sync provider.
//
// A CloudProvider instance is scoped to a particular ledger instance, but can
// be used to sync multiple pages within that ledger.
//
// When delivered from the server, notifications come along with their
// timestamps. These timestamps are server timestamps , i.e. they represent the
// time of registering the notification on the server. Their meaning is
// opaque to the client and depends on the particular service provider, but they
// can be used to make scoped queries - see GetNotifications(),
// WatchNotifications().
class CloudProvider {
 public:
  CloudProvider() {}
  virtual ~CloudProvider() {}

  // Adds the given notification to the cloud. The given callback will be
  // called asynchronously with Status::OK if the operation have succeeded.
  virtual void AddNotification(const PageId& page_id,
                               const Notification& notification,
                               const std::function<void(Status)>& callback) = 0;

  // Registers the given watcher to be notified about notifications
  // already present and these being added to the cloud later. This includes
  // notifications added by the same CloudProvider instance through
  // AddNotification().
  //
  // |watcher| is firstly notified about all notifications already
  // present in the cloud. Then, it is notified about new notifications
  // as they are registered. This allows the client to avoid the race condition
  // when a notification is registered in the cloud between pulling down
  // a list of notifications and establishing a watcher for a new one.
  //
  // Only notifications not older than |min_timestamp| are passed to the
  // |watcher|. Passing empty |min_timestamp| covers all notifications.
  //
  // Each |watcher| object can be registered only once at a time.
  virtual void WatchNotifications(const PageId& page_id,
                                  const std::string& min_timestamp,
                                  NotificationWatcher* watcher) = 0;

  // Unregisters the given watcher. No methods on the watcher will be called
  // after this returns.
  virtual void UnwatchNotifications(NotificationWatcher* watcher) = 0;

  // Retrieves notifications not older than the given |min_timestamp|.
  // Passing empty |min_timestamp| retrieves all notifications.
  //
  // Result is a vector of pairs of the retrieved notifications and their
  // corresponding server timestamps.
  virtual void GetNotifications(
      const PageId& page_id,
      const std::string& min_timestamp,
      std::function<void(Status, const std::vector<Record>&)> callback) = 0;

  // Uploads the given object to the cloud under the given id.
  virtual void AddObject(ObjectIdView object_id,
                         mx::vmo data,
                         std::function<void(Status)> callback) = 0;

  // Retrieves the object of the given id from the cloud. The size of the object
  // is passed to the callback along with the data pipe handle, so that the
  // client can verify that all data was streamed when draining the pipe.
  virtual void GetObject(
      ObjectIdView object_id,
      std::function<void(Status status,
                         uint64_t size,
                         mx::datapipe_consumer data)> callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProvider);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_
