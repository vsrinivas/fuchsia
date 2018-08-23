// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CMX_FACETS_H_
#define GARNET_LIB_CMX_FACETS_H_

#include <string>
#include <unordered_map>

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/allocators.h"

namespace component {

class FacetsMetadata {
 public:
  // Takes in a parsed value assumed to be corresponding to the "facets"
  // attribute. Returns false if parsing failed.
  bool Parse(const rapidjson::Value& facets_value,
             json::JSONParser* json_parser);

  // Returns section value if found, else returns null value.
  const rapidjson::Value& GetSection(const std::string& key) const;

 private:
  rapidjson::MemoryPoolAllocator<> allocator_;
  std::unordered_map<std::string, rapidjson::Value> sections_;
  rapidjson::Value null_value_;
};

}  // namespace component

#endif  // GARNET_LIB_CMX_FACETS_H_
