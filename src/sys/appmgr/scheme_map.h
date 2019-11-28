// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_SCHEME_MAP_H_
#define SRC_SYS_APPMGR_SCHEME_MAP_H_

#include <string>
#include <unordered_map>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {

// Represents a mapping from file scheme to component type. Generated from
// scheme_map configuration file.
class SchemeMap {
 public:
  SchemeMap() = default;

  // Parses a scheme map from a directory containing scheme map config files.
  // Each file adds more scheme->launcher mappings.
  bool ParseFromDirectory(const std::string& path);

  // Like |ParseFromDirectory|, but with a path relative to an open directory,
  // rather than relative to an implicit working directory.
  bool ParseFromDirectoryAt(fxl::UniqueFD& dir, const std::string& path);

  bool HasError() const { return json_parser_.HasError(); }
  std::string error_str() const { return json_parser_.error_str(); }

  // Returns the launcher type for a given scheme, or "" if none.
  std::string LookUp(const std::string& scheme) const;

  static const char kConfigDirPath[];

 private:
  void ParseDocument(rapidjson::Document document);

  std::unordered_map<std::string, std::string> internal_map_;
  json::JSONParser json_parser_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SchemeMap);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_SCHEME_MAP_H_
