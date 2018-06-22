// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_RUNTIME_METADATA_H_
#define GARNET_BIN_APPMGR_RUNTIME_METADATA_H_

#include <string>

namespace component {

class RuntimeMetadata {
 public:
  RuntimeMetadata();
  ~RuntimeMetadata();

  bool Parse(const std::string& data);

  const std::string& runner() const { return runner_; }

 private:
  std::string runner_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_RUNTIME_METADATA_H_
