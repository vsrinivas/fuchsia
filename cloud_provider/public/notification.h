// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_
#define APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_

#include <map>
#include <string>

#include "apps/ledger/cloud_provider/public/types.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

// Represents a notification.
struct Notification {
  Notification();
  Notification(NotificationId&& id,
               Data&& content,
               std::map<StorageObjectId, Data>&& inline_storage_objects);

  ~Notification();

  Notification(Notification&&);
  Notification& operator=(Notification&&);

  bool operator==(const Notification& other) const;

  Notification Clone() const;

  // The notification id.
  NotificationId id;

  // The notification content.
  Data content;

  // The inline storage objects.
  std::map<StorageObjectId, Data> storage_objects;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Notification);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_NOTIFICATION_H_
