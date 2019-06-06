// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <string>
#include <utility>
#include <vector>

#include "lib/json/json_parser.h"
#include "rapidjson/document.h"
#include "src/lib/fxl/macros.h"

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
//    },
//    "system-services": [
//      "fuchsia.netstack.Netstack"
//    ]
//  }
// }
static constexpr char kFuchsiaTest[] = "fuchsia.test";

class TestMetadata {
  using Services =
      std::vector<std::pair<std::string, fuchsia::sys::LaunchInfo>>;

 public:
  TestMetadata();
  ~TestMetadata();

  bool ParseFromString(const std::string& cmx_data,
                       const std::string& filename);

  bool HasError() const { return json_parser_.HasError(); }
  std::string error_str() const { return json_parser_.error_str(); }

  bool is_null() const { return null_; }

  bool HasServices() const { return !service_url_pair_.empty(); }
  Services TakeServices() { return std::move(service_url_pair_); }
  const std::vector<std::string>& system_services() const {
    return system_services_;
  }

 private:
  fuchsia::sys::LaunchInfo GetLaunchInfo(
      const rapidjson::Document::ValueType& value, const std::string& name);

  json::JSONParser json_parser_;
  bool null_ = true;
  Services service_url_pair_;

  std::vector<std::string> system_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestMetadata);
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_TEST_METADATA_H_
