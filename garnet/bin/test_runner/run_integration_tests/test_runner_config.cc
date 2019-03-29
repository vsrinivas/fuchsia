// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/test_runner/run_integration_tests/test_runner_config.h"

#include <sys/socket.h>

#include <iostream>
#include <string>

#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "rapidjson/document.h"

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
    FXL_CHECK(test.HasMember("name"));
    FXL_CHECK(test["name"].IsString());
    FXL_CHECK(test.HasMember("exec"));
    FXL_CHECK(test["exec"].IsString() || test["exec"].IsArray());
    FXL_CHECK(!test.HasMember("disabled") || test["disabled"].IsBool());

    std::string test_name = test["name"].GetString();
    bool test_is_disabled = test.HasMember("disabled") &&
                            test["disabled"].GetBool();
    if (test_is_disabled) {
      disabled_test_names_.push_back(test_name);
      continue;
    }
    test_names_.push_back(test_name);
    if (test["exec"].IsString()) {
      std::string test_exec = test["exec"].GetString();
      std::vector<std::string> test_exec_args = fxl::SplitStringCopy(
          test_exec, " ",
          fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

      FXL_CHECK(!test_exec_args.empty()) << test_name << ": " << test_exec;
      test_commands_[test_name] = test_exec_args;

    } else if (test["exec"].IsArray()) {
      for (auto& arg : test["exec"].GetArray()) {
        FXL_CHECK(arg.IsString()) << test_name;
        test_commands_[test_name].push_back(arg.GetString());
      }

      FXL_CHECK(!test_commands_[test_name].empty()) << test_name;
      FXL_CHECK(!test_commands_[test_name].front().empty()) << test_name;
    }
  }
}

bool TestRunnerConfig::HasTestNamed(const std::string& test_name) const {
  return test_commands_.find(test_name) != test_commands_.end();
}

const std::vector<std::string>& TestRunnerConfig::GetTestCommand(
    const std::string& test_name) const {
  static const std::vector<std::string> empty;
  const auto& i = test_commands_.find(test_name);
  if (i == test_commands_.end()) {
    return empty;
  }
  return i->second;
}

}  // namespace test_runner
