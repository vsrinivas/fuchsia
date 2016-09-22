// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_
#define APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_

#include <map>
#include <string>
#include <utility>

#include "apps/ledger/cloud_provider/public/types.h"

namespace cloud_provider {

// Represents a notification.
class Notification {
 public:
  Notification(const NotificationId& id,
               const Data& content,
               const std::map<StorageObjectId, Data>& inline_storage_objects);
  ~Notification();

  // Returns the notification id.
  const NotificationId& GetId() const { return id_; }

  // Returns the notification content.
  const Data& GetContent() const { return content_; }

  // Returns the inline storage objects.
  const std::map<StorageObjectId, Data>& GetStorageObjects() const {
    return storage_objects_;
  }

  bool operator==(const Notification& other) const;

 private:
  const NotificationId id_;
  const Data content_;
  const std::map<StorageObjectId, Data> storage_objects_;
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_
