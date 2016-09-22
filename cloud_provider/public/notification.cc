// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider/public/notification.h"

namespace cloud_provider {

Notification::Notification(
    const NotificationId& id,
    const Data& content,
    const std::map<StorageObjectId, Data>& storage_objects)
    : id_(id), content_(content), storage_objects_(storage_objects) {}

Notification::~Notification() {}

bool Notification::operator==(const Notification& other) const {
  return id_ == other.id_ && content_ == other.content_ &&
         storage_objects_ == other.storage_objects_;
}

}  // namespace cloud_provider
