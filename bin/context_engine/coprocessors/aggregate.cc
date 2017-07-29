// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/coprocessors/aggregate.h"

#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

namespace {

ContextValue AggregateJSONStrings(
    const std::vector<const ContextValue*>& values) {
  rapidjson::Document out(rapidjson::kArrayType);
  for (const auto* value : values) {
    rapidjson::Document d;
    d.Parse(value->json);
    if (d.HasParseError()) {
      FTL_LOG(ERROR) << "JSON parse error: " << value->json;
      continue;
    }

    if (d.IsArray()) {
      // Take every element in |d| and concatenate it to |out|.
      for (rapidjson::SizeType i = 0; i < d.Size(); ++i) {
        rapidjson::Value v(
            d[i], out.GetAllocator());  // Copy d[i] to out's allocator.
        out.PushBack(v, out.GetAllocator());
      }
    } else {
      // Just concatenate the value as-is.
      rapidjson::Value v(d, out.GetAllocator());
      out.PushBack(v, out.GetAllocator());
    }
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  out.Accept(writer);

  // TODO(thatguy): this is just a hold-over to preserve any metadata.
  // It's terrible. Fix it along with MW-137.
  ContextValue new_value;
  new_value.json = buffer.GetString();
  new_value.meta = values[0]->meta.Clone();
  return new_value;
}

}  // namespace

AggregateCoprocessor::AggregateCoprocessor(
    const std::string& topic_to_aggregate)
    : topic_to_aggregate_(topic_to_aggregate) {}

AggregateCoprocessor::~AggregateCoprocessor() = default;

void AggregateCoprocessor::ProcessTopicUpdate(
    const ContextRepository* repository,
    const std::set<std::string>& topics_updated,
    std::map<std::string, ContextValue>* out) {
  for (const auto& topic : topics_updated) {
    std::string story_id;
    std::string module_id;
    std::string local_topic_id;
    if (!ParseModuleScopeTopic(topic, &story_id, &module_id, &local_topic_id)) {
      continue;
    }

    if (local_topic_id == topic_to_aggregate_) {
      // Get all |topic_to_aggregate_| across the same story, and aggregate
      // them.
      std::vector<const ContextValue*> values;
      repository->GetAllValuesInStoryScope(story_id, topic_to_aggregate_,
                                           &values);

      out->emplace(MakeStoryScopeTopic(story_id, topic_to_aggregate_),
                   AggregateJSONStrings(values));
    }
  }
}

}  // namespace maxwell
