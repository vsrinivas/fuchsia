// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SCHEME_MAP_H
#define GARNET_BIN_APPMGR_SCHEME_MAP_H

#include <string>
#include <unordered_map>

#include <lib/fxl/macros.h>

namespace component {

// Represents a mapping from file scheme to component type. Generated from
// scheme_map configuration file.
class SchemeMap {
 public:
  SchemeMap() = default;

  // Parses a JSON schema map config string and uses it to initialize this
  // object.
  bool Parse(const std::string& data, std::string* error);

  // Returns the launcher type for a given scheme, or "" if none.
  std::string LookUp(const std::string& scheme) const;

  // Returns the path for the scheme map config file.
  static std::string GetSchemeMapPath();

 private:
  std::unordered_map<std::string, std::string> internal_map_;
  bool parsed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SchemeMap);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SCHEME_MAP_H
