// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CMX_FACET_PARSER_CMX_FACET_PARSER_H_
#define LIB_CMX_FACET_PARSER_CMX_FACET_PARSER_H_

#include <regex>
#include <string>
#include <unordered_map>

#include "lib/json/json_parser.h"
#include "rapidjson/document.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

// CmxFacetParser is used to parse facets out of a cmx metadata file.
class CmxFacetParser {
 public:
  CmxFacetParser();
  ~CmxFacetParser();

  // Takes in a parsed JSON document and parses out the different facets. After
  // calling |Parse()| (or the |ParseFromFileAt| variant below), |GetSection()|
  // may be used to retrieve a particular facet.
  bool Parse(const rapidjson::Document& document, json::JSONParser* json_parser);

  // Like |Parse()|, but parses the json file in |file|, which is rooted at
  // |dirfd|. Returns false if there were any errors.
  bool ParseFromFileAt(int dirfd, const std::string& file, json::JSONParser* json_parser);

  // Returns section value if found, else returns null value.
  const rapidjson::Value& GetSection(const std::string& key) const;

 private:
  rapidjson::MemoryPoolAllocator<> allocator_;
  std::unordered_map<std::string, rapidjson::Value> sections_;
  rapidjson::Value null_value_;
};

}  // namespace component

#endif  // LIB_CMX_FACET_PARSER_CMX_FACET_PARSER_H_
