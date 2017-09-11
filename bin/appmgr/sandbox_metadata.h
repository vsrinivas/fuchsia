// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_SANDBOX_METADATA_H_
#define APPLICATION_SRC_MANAGER_SANDBOX_METADATA_H_

#include <string>
#include <vector>

namespace app {

class SandboxMetadata {
 public:
  SandboxMetadata();
  ~SandboxMetadata();

  bool Parse(const std::string& data);

  const std::vector<std::string>& dev() const { return dev_; }
  const std::vector<std::string>& features() const { return features_; }

 private:
  std::vector<std::string> dev_;
  std::vector<std::string> features_;
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_SANDBOX_METADATA_H_
