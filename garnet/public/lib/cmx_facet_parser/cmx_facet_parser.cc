// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/cmx_facet_parser/cmx_facet_parser.h"

#include <trace/event.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

#include "rapidjson/document.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

constexpr char kFacets[] = "facets";

CmxFacetParser::CmxFacetParser() = default;
CmxFacetParser::~CmxFacetParser() = default;

bool CmxFacetParser::Parse(const rapidjson::Document& document, json::JSONParser* json_parser) {
  auto facets = document.FindMember(kFacets);
  if (facets == document.MemberEnd()) {
    // Valid syntax, but no value.
    return true;
  }

  sections_.clear();
  if (!facets->value.IsObject()) {
    json_parser->ReportError("Facets is not an object.");
    return false;
  }

  for (rapidjson::Value::ConstMemberIterator itr = facets->value.MemberBegin();
       itr != facets->value.MemberEnd(); ++itr) {
    std::string key = itr->name.GetString();
    rapidjson::Value value(itr->value, allocator_);
    sections_.insert({key, std::move(value)});
  }
  return true;
}

bool CmxFacetParser::ParseFromFileAt(int dirfd, const std::string& file,
                                     json::JSONParser* json_parser) {
  TRACE_DURATION("cmx", "CmxFacetParser::ParseFromFileAt", "file", file);
  rapidjson::Document document = json_parser->ParseFromFileAt(dirfd, file);
  if (json_parser->HasError()) {
    return false;
  }
  if (!document.IsObject()) {
    json_parser->ReportError("File is not a JSON object.");
    return false;
  }
  return Parse(document, json_parser);
}

const rapidjson::Value& CmxFacetParser::GetSection(const std::string& key) const {
  auto map_entry = sections_.find(key);
  if (map_entry == sections_.end()) {
    return null_value_;
  }
  return map_entry->second;
}

}  // namespace component
