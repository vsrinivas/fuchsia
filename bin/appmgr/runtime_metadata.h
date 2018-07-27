// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_RUNTIME_METADATA_H_
#define GARNET_BIN_APPMGR_RUNTIME_METADATA_H_

#include <string>

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

class RuntimeMetadata {
 public:
  // Returns true if parsing succeeded. If a config is missing the runtime but
  // otherwise there are no errors, parsing succeeds and IsNull() is true.
  // |json_parser| is used to report any errors.
  bool ParseFromFileAt(int dirfd, const std::string& file,
                       json::JSONParser* json_parser);
  bool ParseFromDocument(const rapidjson::Document& document,
                         json::JSONParser* json_parser);

  bool IsNull() const { return null_; }
  const std::string& runner() const { return runner_; }

 private:
  bool null_ = true;
  std::string runner_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_RUNTIME_METADATA_H_
