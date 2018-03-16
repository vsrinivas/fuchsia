// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SANDBOX_METADATA_H_
#define GARNET_BIN_APPMGR_SANDBOX_METADATA_H_

#include <string>
#include <vector>

namespace component {

class SandboxMetadata {
 public:
  SandboxMetadata();
  ~SandboxMetadata();

  bool Parse(const std::string& data);
  bool HasFeature(const std::string& feature);

  const std::vector<std::string>& dev() const { return dev_; }
  const std::vector<std::string>& system() const { return system_; }
  const std::vector<std::string>& pkgfs() const { return pkgfs_; }
  const std::vector<std::string>& features() const { return features_; }

 private:
  std::vector<std::string> dev_;
  std::vector<std::string> system_;
  std::vector<std::string> pkgfs_;
  std::vector<std::string> features_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SANDBOX_METADATA_H_
