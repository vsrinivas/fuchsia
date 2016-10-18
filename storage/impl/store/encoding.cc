// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/encoding.h"

#include "apps/ledger/glue/crypto/base64.h"
#include "lib/ftl/logging.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace storage {

namespace {

const int kPriorityEager = 0;
const int kPriorityLazy = 1;

const char kEntries[] = "entries";
const char kKey[] = "key";
const char kObjectId[] = "object_id";
const char kPriority[] = "priority";
const char kChildren[] = "children";

void WriteAsBase64(rapidjson::Writer<rapidjson::StringBuffer>& writer,
                   ftl::StringView bytes) {
  std::string encoded;
  glue::Base64Encode(bytes, &encoded);
  writer.String(encoded.c_str(), encoded.size());
}

bool ReadFromBase64(const rapidjson::Value& value, std::string* decoded) {
  if (!value.IsString()) {
    return false;
  }

  std::string result;
  return glue::Base64Decode(
      ftl::StringView(value.GetString(), value.GetStringLength()), decoded);
}

}  // namespace

std::string EncodeNode(const std::vector<Entry>& entries,
                       const std::vector<ObjectId>& children) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();
  {
    writer.Key(kEntries);
    writer.StartArray();
    {
      for (const Entry& entry : entries) {
        writer.StartObject();
        {
          writer.Key(kKey);
          WriteAsBase64(writer, entry.key);

          writer.Key(kObjectId);
          WriteAsBase64(writer, entry.object_id);

          writer.Key(kPriority);
          if (entry.priority == KeyPriority::EAGER) {
            writer.Int(kPriorityEager);
          } else if (entry.priority == KeyPriority::LAZY) {
            writer.Int(kPriorityLazy);
          } else {
            FTL_NOTREACHED();
          }
        }
        writer.EndObject();
      }
    }
    writer.EndArray();

    writer.Key(kChildren);
    writer.StartArray();
    {
      for (const ObjectId& child : children) {
        WriteAsBase64(writer, child);
      }
    }
    writer.EndArray();
  }
  writer.EndObject();

  return string_buffer.GetString();
}

bool DecodeNode(ftl::StringView json,
                std::vector<Entry>* res_entries,
                std::vector<ObjectId>* res_children) {
  rapidjson::Document document;
  document.Parse(json.data(), json.size());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  if (!document.HasMember(kEntries) || !document[kEntries].IsArray()) {
    return false;
  }

  if (!document.HasMember(kChildren) || !document[kEntries].IsArray()) {
    return false;
  }

  std::vector<Entry> entries;
  std::vector<ObjectId> children;

  for (auto& it : document[kEntries].GetArray()) {
    Entry entry;
    if (!it.IsObject()) {
      return false;
    }

    if (!it.HasMember(kKey) || !ReadFromBase64(it[kKey], &entry.key)) {
      return false;
    }

    if (!it.HasMember(kObjectId) ||
        !ReadFromBase64(it[kObjectId], &entry.object_id)) {
      return false;
    }

    if (!it.HasMember(kPriority) || !it[kPriority].IsInt()) {
      return false;
    }

    if (it[kPriority].GetInt() == kPriorityEager) {
      entry.priority = KeyPriority::EAGER;
    } else if (it[kPriority].GetInt() == kPriorityLazy) {
      entry.priority = KeyPriority::LAZY;
    } else {
      return false;
    }

    entries.push_back(entry);
  }

  for (auto& it : document[kChildren].GetArray()) {
    ObjectId child;
    if (!ReadFromBase64(it, &child)) {
      return false;
    }
    children.push_back(child);
  }

  res_entries->swap(entries);
  res_children->swap(children);
  return true;
}
}  // namespace storage
