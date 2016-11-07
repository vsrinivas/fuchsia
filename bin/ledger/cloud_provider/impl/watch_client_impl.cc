// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/watch_client_impl.h"

#include "apps/ledger/src/cloud_provider/impl/encoding.h"
#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

namespace cloud_provider {

WatchClientImpl::WatchClientImpl(firebase::Firebase* firebase,
                                 const std::string& firebase_key,
                                 const std::string& query,
                                 NotificationWatcher* notification_watcher)
    : firebase_(firebase), notification_watcher_(notification_watcher) {
  firebase_->Watch(firebase_key, query, this);
}

WatchClientImpl::~WatchClientImpl() {
  firebase_->UnWatch(this);
}

void WatchClientImpl::OnPut(const std::string& path,
                            const rapidjson::Value& value) {
  if (!value.IsObject()) {
    FTL_LOG(ERROR) << "Ignoring a malformed notification from Firebase. "
                   << "Returned data is not a dictionary.";
    return;
  }

  if (path == "/") {
    // The initial put event contains multiple notifications.
    std::vector<Record> records;
    if (!DecodeMultipleNotificationsFromValue(value, &records)) {
      FTL_LOG(ERROR) << "Ignoring a malformed notification from Firebase. "
                     << "Can't decode a collection of notifications.";
      return;
    }
    for (size_t i = 0u; i < records.size(); i++) {
      notification_watcher_->OnNewNotification(records[i].notification,
                                               records[i].timestamp);
    }
    return;
  }

  if (path.empty() || path.front() != '/') {
    FTL_LOG(ERROR) << "Ignoring a malformed notification from Firebase. "
                   << path << " is not a valid path.";
    return;
  }

  std::unique_ptr<Record> record;
  if (!DecodeNotificationFromValue(value, &record)) {
    FTL_LOG(ERROR) << "Ignoring a malformed notification from Firebase. "
                   << "Can't decode the notification.";
    return;
  }

  notification_watcher_->OnNewNotification(record->notification,
                                           record->timestamp);
}

void WatchClientImpl::OnError() {
  // TODO(ppi): add an error callback on the NotificationWatcher and
  // surface this there.
  FTL_LOG(ERROR) << "Firebase client signalled an unknown error.";
}

}  // namespace cloud_provider
