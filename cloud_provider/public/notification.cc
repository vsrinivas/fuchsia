// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider/public/notification.h"

#include <utility>

namespace cloud_provider {

Notification::Notification() = default;

Notification::Notification(NotificationId&& id,
                           Data&& content,
                           std::map<StorageObjectId, Data>&& storage_objects)
    : id(std::move(id)),
      content(std::move(content)),
      storage_objects(std::move(storage_objects)) {}

Notification::~Notification() = default;

Notification::Notification(Notification&&) = default;

Notification& Notification::operator=(Notification&&) = default;

bool Notification::operator==(const Notification& other) const {
  return id == other.id && content == other.content &&
         storage_objects == other.storage_objects;
}

Notification Notification::Clone() const {
  Notification clone;
  clone.id = id;
  clone.content = content;
  clone.storage_objects = storage_objects;
  return clone;
}

}  // namespace cloud_provider
