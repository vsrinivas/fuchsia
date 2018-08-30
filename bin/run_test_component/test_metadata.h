// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_

#include <string>
#include <utility>
#include <vector>

#include "garnet/lib/json/json_parser.h"

namespace run {

// This is a section in "facets" for component manifest. This section is used to
// define if there is something extra that tests needs before it is launched.
// Currently we support "injected-services" which define what all services need
// to be started by this utility in test's hermetic environment.
//
// example:
// "facets": {
//  "fuchsia.test": {
//    "injected-services": {
//      "fuchsia.log.LogSink": "logger",
//      "fuchsia.log.Log": "logger"
//    }
//  }
// }
static constexpr char kFuchsiaTest[] = "fuchsia.test";

class TestMetadata {
 public:
  bool ParseFromFile(const std::string& cmx_file_path);

  bool HasError() const { return json_parser_.HasError(); }
  std::string error_str() const {
    return json_parser_.error_str();
  }

  bool is_null() const { return null_; }
  const std::vector<std::pair<std::string, std::string>>& services() const {
    return service_url_pair_;
  }

 private:
  json::JSONParser json_parser_;
  bool null_ = true;
  std::vector<std::pair<std::string, std::string>> service_url_pair_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_
