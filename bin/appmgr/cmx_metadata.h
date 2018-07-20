// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_CMX_METADATA_H_
#define GARNET_BIN_APPMGR_CMX_METADATA_H_

#include <string>
#include <vector>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

class CmxMetadata {
 public:
  CmxMetadata();
  ~CmxMetadata();

  // Takes a raw JSON string and returns the value object corresponding to
  // "sandbox".
  bool ParseSandboxMetadata(const std::string& data,
                            rapidjson::Value* parsed_value);

  // Takes a package's resolved_url, e.g. file:///pkgfs/packages/<FOO>/0, and
  // returns the default component's .cmx path, e.g. meta/<FOO>.cmx
  static std::string GetCmxPath(const std::string& resolved_url);

  // Returns true if path ends in .cmx, false otherwise
  static bool IsCmxExtension(const std::string& path);

  // Returns the package name from a .cmx file's full /pkgfs path. Returns the
  // empty string "" if unmatched.
  static std::string GetPackageNameFromCmxPath(const std::string& cmx_path);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_CMX_METADATA_H_
