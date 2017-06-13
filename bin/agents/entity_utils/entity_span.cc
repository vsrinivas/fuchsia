// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/agents/entity_utils/entity_span.h"

#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

EntitySpan::EntitySpan(std::string content, std::string type, int start, int end)
    : content_(content), type_(type), start_(start), end_(end) {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject();
  writer.Key("content");
  writer.String(content);
  writer.Key("type");
  writer.String(type);
  writer.Key("start");
  writer.Uint(start_);
  writer.Key("end");
  writer.Uint(end_);
  writer.EndObject();
  json_string_ = s.GetString();
}

}  // namespace maxwell
