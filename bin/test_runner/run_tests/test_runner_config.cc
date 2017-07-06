// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/src/run_tests/test_runner_config.h"

#include <sys/socket.h>

#include <iostream>
#include <string>

#include "lib/ftl/files/file.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace test_runner {

TestRunnerConfig::TestRunnerConfig(const std::string& json_path) {
  std::string json;
  FTL_CHECK(files::ReadFileToString(json_path, &json));

  rapidjson::Document doc;
  doc.Parse(json);
  FTL_CHECK(doc.IsObject());

  auto& tests = doc["tests"];
  FTL_CHECK(tests.IsArray());

  for (auto& test : tests.GetArray()) {
    FTL_CHECK(test.IsObject());
    std::string test_name = test["name"].GetString();
    std::string test_exec = test["exec"].GetString();
    test_names_.push_back(test_name);
    test_commands_[test_name] = test_exec;
  }
}

}  // namespace test_runner
