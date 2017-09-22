// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/agents/entity_utils/entity_span.h"

#include "peridot/lib/rapidjson/rapidjson.h"
#include "lib/fxl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

EntitySpan::EntitySpan(const std::string& content,
                       const std::string& type,
                       const int start,
                       const int end) {
  this->Init(content, type, start, end);
}

EntitySpan EntitySpan::FromJson(const std::string& json_string) {
  rapidjson::Document e;
  e.Parse(json_string);
  if (e.HasParseError() ||
      !(e.HasMember("content") && e["content"].IsString() &&
        e.HasMember("type") && e["type"].IsString() && e.HasMember("start") &&
        e["start"].IsInt() && e.HasMember("end") && e["end"].IsInt())) {
    // TODO(travismart): Validate this with rapidjson schema validation.
    FXL_LOG(ERROR) << "Invalid parsing of Entity from JSON: " << json_string;
  }
  return EntitySpan(e["content"].GetString(), e["type"].GetString(),
                    e["start"].GetInt(), e["end"].GetInt());
}

std::vector<EntitySpan> EntitySpan::FromContextValues(
    const fidl::Array<ContextValuePtr>& values) {
  std::vector<EntitySpan> entities;
  for (const auto& value : values) {
    entities.push_back(EntitySpan::FromJson(value->content));
  }
  return entities;
}

void EntitySpan::Init(const std::string& content,
                      const std::string& type,
                      const int start,
                      const int end) {
  content_ = content;
  type_ = type;
  start_ = start;
  end_ = end;

  rapidjson::Document d;
  auto& allocator = d.GetAllocator();
  rapidjson::Value entity(rapidjson::kObjectType);
  entity.AddMember("content", content, allocator);
  entity.AddMember("type", type, allocator);
  entity.AddMember("start", start, allocator);
  entity.AddMember("end", end, allocator);
  json_string_ = modular::JsonValueToString(entity);
}

}  // namespace maxwell
