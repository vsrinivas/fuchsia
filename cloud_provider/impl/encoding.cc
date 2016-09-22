// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider/impl/encoding.h"

#include "apps/ledger/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/firebase/encoding.h"
#include "lib/ftl/logging.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace cloud_provider {

namespace {

const char kIdKey[] = "id";
const char kContentKey[] = "content";
const char kObjectsKey[] = "objects";
const char kTimestampKey[] = "timestamp";

}  // namespace

bool EncodeNotification(const Notification& notification,
                        std::string* output_json) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key(kIdKey);
  writer.String(firebase::EncodeValue(notification.GetId()).c_str());

  writer.Key(kContentKey);
  writer.String(firebase::EncodeValue(notification.GetContent()).c_str());

  if (!notification.GetStorageObjects().empty()) {
    writer.Key(kObjectsKey);
    writer.StartObject();
    for (const auto& entry : notification.GetStorageObjects()) {
      writer.Key(firebase::EncodeKey(entry.first).c_str());
      writer.String(firebase::EncodeValue(entry.second).c_str());
    }
    writer.EndObject();
  }

  writer.Key(kTimestampKey);
  // Placeholder that Firebase will replace with server timestamp. See
  // https://firebase.google.com/docs/database/rest/save-data.
  writer.StartObject();
  writer.Key(".sv");
  writer.String("timestamp");
  writer.EndObject();

  writer.EndObject();

  if (!writer.IsComplete()) {
    return false;
  }

  std::string result = string_buffer.GetString();
  output_json->swap(result);
  return true;
}

bool DecodeNotification(const std::string& json,
                        std::unique_ptr<Record>* output_record) {
  rapidjson::Document document;
  document.Parse(json.c_str());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  return DecodeNotificationFromValue(document, output_record);
}

bool DecodeMultipleNotifications(const std::string& json,
                                 std::vector<Record>* output_records) {
  rapidjson::Document document;
  document.Parse(json.c_str());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  return DecodeMultipleNotificationsFromValue(document, output_records);
}

bool DecodeMultipleNotificationsFromValue(const rapidjson::Value& value,
                                          std::vector<Record>* output_records) {
  FTL_DCHECK(output_records);
  FTL_DCHECK(value.IsObject());

  std::vector<Record> records;
  for (auto& it : value.GetObject()) {
    std::string encoded_id = it.name.GetString();

    if (!it.value.IsObject()) {
      return false;
    }

    std::unique_ptr<Record> record;
    if (!DecodeNotificationFromValue(it.value, &record)) {
      return false;
    }
    FTL_DCHECK(record);
    records.push_back(*record);
  }

  output_records->swap(records);
  return true;
}

bool DecodeNotificationFromValue(const rapidjson::Value& value,
                                 std::unique_ptr<Record>* output_record) {
  FTL_DCHECK(output_record);
  FTL_DCHECK(value.IsObject());

  NotificationId notification_id;
  if (!value.HasMember(kIdKey) || !value[kIdKey].IsString() ||
      !firebase::Decode(value[kIdKey].GetString(), &notification_id)) {
    return false;
  }

  Data commit_content;
  if (!value.HasMember(kContentKey) || !value[kContentKey].IsString() ||
      !firebase::Decode(value[kContentKey].GetString(), &commit_content)) {
    return false;
  }

  std::map<StorageObjectId, Data> storage_objects;
  if (value.HasMember(kObjectsKey)) {
    for (auto& it : value[kObjectsKey].GetObject()) {
      StorageObjectId storage_object_id;
      if (!firebase::Decode(it.name.GetString(), &storage_object_id)) {
        return false;
      }

      Data storage_object_data;
      if (!it.value.IsString() ||
          !firebase::Decode(it.value.GetString(), &storage_object_data)) {
        return false;
      }
      storage_objects[storage_object_id] = storage_object_data;
    }
  }

  if (!value.HasMember(kTimestampKey) || !value[kTimestampKey].IsNumber()) {
    return false;
  }

  std::unique_ptr<Record> record(
      new Record(Notification(notification_id, commit_content, storage_objects),
                 ServerTimestampToBytes(value[kTimestampKey].GetInt64())));
  output_record->swap(record);
  return true;
}

}  // namespace cloud_provider
