// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/encoding.h"

#include <algorithm>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/fxl/logging.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"
#include "peridot/lib/firebase/encoding.h"

namespace cloud_provider_firebase {

namespace {

const char kIdKey[] = "id";
const char kContentKey[] = "content";
const char kTimestampKey[] = "timestamp";
const char kBatchPositionKey[] = "batch_position";
const char kBatchSizeKey[] = "batch_size";

void WriteCommit(rapidjson::Writer<rapidjson::StringBuffer>* writer,
                 const Commit& commit, std::string encoded_id,
                 int batch_position, int batch_size) {
  writer->StartObject();
  {
    writer->Key(kIdKey);
    writer->String(encoded_id.c_str(), encoded_id.size());

    writer->Key(kContentKey);
    std::string content = firebase::EncodeValue(commit.content);
    writer->String(content.c_str(), content.size());

    writer->Key(kTimestampKey);
    // Placeholder that Firebase will replace with server timestamp. See
    // https://firebase.google.com/docs/database/rest/save-data.
    writer->StartObject();
    {
      writer->Key(".sv");
      writer->String("timestamp");
    }
    writer->EndObject();

    writer->Key(kBatchPositionKey);
    writer->Int(batch_position);

    writer->Key(kBatchSizeKey);
    writer->Int(batch_size);
  }
  writer->EndObject();
}

}  // namespace

bool EncodeCommits(const std::vector<Commit>& commits,
                   std::string* output_json) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();
  {
    for (size_t i = 0; i < commits.size(); i++) {
      std::string encoded_id = firebase::EncodeValue(commits[i].id);
      writer.Key(encoded_id.c_str(), encoded_id.size());
      WriteCommit(&writer, commits[i], std::move(encoded_id), i,
                  commits.size());
    }
  }
  writer.EndObject();

  FXL_DCHECK(writer.IsComplete());

  std::string result = string_buffer.GetString();
  output_json->swap(result);
  return true;
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
  FXL_DCHECK(output_records);
  FXL_DCHECK(value.IsObject());

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
    FXL_DCHECK(record);
    records.push_back(std::move(*record));
  }

  std::sort(records.begin(), records.end(),
            [](const Record& lhs, const Record& rhs) {
              if (lhs.timestamp != rhs.timestamp) {
                return BytesToServerTimestamp(lhs.timestamp) <
                       BytesToServerTimestamp(rhs.timestamp);
              }
              return lhs.batch_position < rhs.batch_position;
            });

  output_records->swap(records);
  return true;
}

bool DecodeCommitFromValue(const rapidjson::Value& value,
                           std::unique_ptr<Record>* output_record) {
  FXL_DCHECK(output_record);
  FXL_DCHECK(value.IsObject());

  // TODO(ppi): use a JSON schema to validate the format.
  CommitId commit_id;
  if (!value.HasMember(kIdKey) || !value[kIdKey].IsString() ||
      !firebase::Decode(value[kIdKey], &commit_id)) {
    return false;
  }

  Data commit_content;
  if (!value.HasMember(kContentKey) || !value[kContentKey].IsString() ||
      !firebase::Decode(value[kContentKey], &commit_content)) {
    return false;
  }

  if (!value.HasMember(kTimestampKey) || !value[kTimestampKey].IsNumber()) {
    return false;
  }

  int batch_position = 0;
  if (value.HasMember(kBatchPositionKey) && value[kBatchPositionKey].IsInt()) {
    batch_position = value[kBatchPositionKey].GetInt();
  }

  int batch_size = 1;
  if (value.HasMember(kBatchSizeKey) && value[kBatchSizeKey].IsInt()) {
    batch_size = value[kBatchSizeKey].GetInt();
  }

  auto record = std::make_unique<Record>(
      Commit(std::move(commit_id), std::move(commit_content)),
      ServerTimestampToBytes(value[kTimestampKey].GetInt64()), batch_position,
      batch_size);
  output_record->swap(record);
  return true;
}

}  // namespace cloud_provider_firebase
