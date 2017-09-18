// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/test_runner/run_integration_tests/test_runner_config.h"

#include <sys/socket.h>

#include <iostream>
#include <string>

#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace test_runner {

TestRunnerConfig::TestRunnerConfig(const std::string& json_path) {
  std::string json;
  FXL_CHECK(files::ReadFileToString(json_path, &json));

  rapidjson::Document doc;
  doc.Parse(json);
  FXL_CHECK(doc.IsObject());

  auto& tests = doc["tests"];
  FXL_CHECK(tests.IsArray());

  for (auto& test : tests.GetArray()) {
    FXL_CHECK(test.IsObject());
    std::string test_name = test["name"].GetString();
    std::string test_exec = test["exec"].GetString();
    test_names_.push_back(test_name);
    test_commands_[test_name] = test_exec;
  }
}

}  // namespace test_runner
