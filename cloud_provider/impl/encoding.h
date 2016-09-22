// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_IMPL_ENCODING_H_
#define APPS_LEDGER_CLOUD_PROVIDER_IMPL_ENCODING_H_

#include <memory>
#include <vector>

#include "apps/ledger/cloud_provider/public/notification.h"
#include "apps/ledger/cloud_provider/public/record.h"

#include <rapidjson/document.h>

namespace cloud_provider {

// These methods encode and decode notifications specifically for storing
// in Firebase Realtime Database.

// Encodes a notification as a JSON string suitable for storing in
// Firebase Realtime Database. In addition to the notification content, a
// timestamp placeholder is added, making Firebase tag the notification with a
// server timestamp.
bool EncodeNotification(const Notification& notification,
                        std::string* output_json);

// Decodes a notification from the JSON representation in Firebase
// Realtime Database. If successful, the method returns true, and
// |output_record| contains the decoded notification, along with opaque
// server timestamp.
bool DecodeNotification(const std::string& json,
                        std::unique_ptr<Record>* output_record);

// Decodes multiple notifications from the JSON representation of an
// object holding them in Firebase Realtime Database. If successful, the method
// returns true, and |output_records| contain the decoded notifications
// along with their timestamps.
bool DecodeMultipleNotifications(const std::string& json,
                                 std::vector<Record>* output_records);

bool DecodeNotificationFromValue(const rapidjson::Value& value,
                                 std::unique_ptr<Record>* output_record);

bool DecodeMultipleNotificationsFromValue(const rapidjson::Value& value,
                                          std::vector<Record>* output_records);

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_IMPL_ENCODING_H_
