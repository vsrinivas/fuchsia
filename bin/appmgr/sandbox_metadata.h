// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SANDBOX_METADATA_H_
#define GARNET_BIN_APPMGR_SANDBOX_METADATA_H_

#include <string>
#include <vector>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

// TODO(geb): Use JSONParser to hold errors.
class SandboxMetadata {
 public:
  SandboxMetadata();
  ~SandboxMetadata();

  bool Parse(const rapidjson::Value& sandbox_value);
  bool HasFeature(const std::string& feature) const;
  void AddFeature(std::string feature);

  const std::vector<std::string>& dev() const { return dev_; }
  const std::vector<std::string>& system() const { return system_; }
  const std::vector<std::string>& pkgfs() const { return pkgfs_; }
  const std::vector<std::string>& features() const { return features_; }
  const std::vector<std::string>& boot() const { return boot_; }

  bool IsNull() const { return null_; }

 private:
  bool null_ = true;
  std::vector<std::string> dev_;
  std::vector<std::string> system_;
  std::vector<std::string> pkgfs_;
  std::vector<std::string> features_;
  std::vector<std::string> boot_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SANDBOX_METADATA_H_
