// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/encoding.h"

#include <algorithm>

#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/src/firebase/encoding.h"
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

bool EncodeCommit(const Commit& commit, std::string* output_json) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key(kIdKey);
  std::string id = firebase::EncodeValue(commit.id);
  writer.String(id.c_str(), id.size());

  writer.Key(kContentKey);
  std::string content = firebase::EncodeValue(commit.content);
  writer.String(content.c_str(), content.size());

  if (!commit.storage_objects.empty()) {
    writer.Key(kObjectsKey);
    writer.StartObject();
    for (const auto& entry : commit.storage_objects) {
      std::string key = firebase::EncodeKey(entry.first);
      writer.Key(key.c_str(), key.size());
      std::string value = firebase::EncodeValue(entry.second);
      writer.String(value.c_str(), value.size());
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

bool DecodeCommit(const std::string& json,
                  std::unique_ptr<Record>* output_record) {
  rapidjson::Document document;
  document.Parse(json.c_str(), json.size());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  return DecodeCommitFromValue(document, output_record);
}

bool DecodeMultipleCommits(const std::string& json,
                           std::vector<Record>* output_records) {
  rapidjson::Document document;
  document.Parse(json.c_str(), json.size());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  return DecodeMultipleCommitsFromValue(document, output_records);
}

bool DecodeMultipleCommitsFromValue(const rapidjson::Value& value,
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
    if (!DecodeCommitFromValue(it.value, &record)) {
      return false;
    }
    FTL_DCHECK(record);
    records.push_back(std::move(*record));
  }

  std::sort(records.begin(), records.end(),
            [](const Record& lhs, const Record& rhs) {
              return BytesToServerTimestamp(lhs.timestamp) <
                     BytesToServerTimestamp(rhs.timestamp);
            });

  output_records->swap(records);
  return true;
}

bool DecodeCommitFromValue(const rapidjson::Value& value,
                           std::unique_ptr<Record>* output_record) {
  FTL_DCHECK(output_record);
  FTL_DCHECK(value.IsObject());

  CommitId commit_id;
  if (!value.HasMember(kIdKey) || !value[kIdKey].IsString() ||
      !firebase::Decode(value[kIdKey].GetString(), &commit_id)) {
    return false;
  }

  Data commit_content;
  if (!value.HasMember(kContentKey) || !value[kContentKey].IsString() ||
      !firebase::Decode(value[kContentKey].GetString(), &commit_content)) {
    return false;
  }

  std::map<ObjectId, Data> storage_objects;
  if (value.HasMember(kObjectsKey)) {
    for (auto& it : value[kObjectsKey].GetObject()) {
      ObjectId storage_object_id;
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

  auto record = std::make_unique<Record>(
      Commit(std::move(commit_id), std::move(commit_content),
             std::move(storage_objects)),
      ServerTimestampToBytes(value[kTimestampKey].GetInt64()));
  output_record->swap(record);
  return true;
}

}  // namespace cloud_provider
