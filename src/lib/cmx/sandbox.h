// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CMX_SANDBOX_H_
#define SRC_LIB_CMX_SANDBOX_H_

#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {

class SandboxMetadata {
 public:
  // Returns true if parsing succeeded. |json_parser| is used to report any
  // errors.
  bool Parse(const rapidjson::Value& sandbox_value, json::JSONParser* json_parser);

  bool HasFeature(const std::string& feature) const;
  void AddFeature(std::string feature);

  bool HasService(const std::string& service) const;

  bool HasInternalFeature(const std::string& feature) const;

  const std::vector<std::string>& dev() const { return dev_; }
  const std::vector<std::string>& system() const { return system_; }
  const std::vector<std::string>& services() const { return services_; }
  const std::vector<std::string>& pkgfs() const { return pkgfs_; }
  const std::vector<std::string>& features() const { return features_; }
  const std::vector<std::string>& boot() const { return boot_; }
  const std::vector<std::string>& internal_features() const { return internal_features_; }

  bool IsNull() const { return null_; }

 private:
  bool null_ = true;
  std::vector<std::string> dev_;
  std::vector<std::string> system_;
  std::vector<std::string> services_;
  std::vector<std::string> pkgfs_;
  std::vector<std::string> features_;
  std::vector<std::string> boot_;
  std::vector<std::string> internal_features_;
};

}  // namespace component

#endif  // SRC_LIB_CMX_SANDBOX_H_
