// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/facets.h"

#include "garnet/lib/json/json_parser.h"
#include "rapidjson/document.h"

namespace component {

bool FacetsMetadata::Parse(const rapidjson::Value& facets_value,
                           json::JSONParser* json_parser) {
  sections_.clear();
  if (!facets_value.IsObject()) {
    json_parser->ReportError("Facets is not an object.");
    return false;
  }

  for (rapidjson::Value::ConstMemberIterator itr = facets_value.MemberBegin();
       itr != facets_value.MemberEnd(); ++itr) {
    std::string key = itr->name.GetString();
    rapidjson::Value value(itr->value, allocator_);
    sections_.insert({key, std::move(value)});
  }
  return true;
}

const rapidjson::Value& FacetsMetadata::GetSection(
    const std::string& key) const {
  auto map_entry = sections_.find(key);
  if (map_entry == sections_.end()) {
    return null_value_;
  }
  return map_entry->second;
}

}  // namespace component
