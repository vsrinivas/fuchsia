// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/test_runner/run_integration_tests/test_runner_config.h"

#include <lib/syslog/cpp/macros.h>
#include <sys/socket.h>

#include <iostream>
#include <string>

#include <fbl/unique_fd.h>

#include "rapidjson/document.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace test_runner {

TestRunnerConfig::TestRunnerConfig(const std::string& json_path) {
  std::string json;
  FX_CHECK(files::ReadFileToString(json_path, &json));

  rapidjson::Document doc;
  doc.Parse(json);
  FX_CHECK(doc.IsObject());

  auto& tests = doc["tests"];
  FX_CHECK(tests.IsArray());

  for (auto& test : tests.GetArray()) {
    FX_CHECK(test.IsObject());
    FX_CHECK(test.HasMember("name"));
    FX_CHECK(test["name"].IsString());
    FX_CHECK(test.HasMember("exec"));
    FX_CHECK(test["exec"].IsString() || test["exec"].IsArray());
    FX_CHECK(!test.HasMember("disabled") || test["disabled"].IsBool());

    std::string test_name = test["name"].GetString();
    bool test_is_disabled = test.HasMember("disabled") && test["disabled"].GetBool();
    if (test_is_disabled) {
      disabled_test_names_.push_back(test_name);
      continue;
    }
    test_names_.push_back(test_name);
    if (test["exec"].IsString()) {
      std::string test_exec = test["exec"].GetString();
      std::vector<std::string> test_exec_args =
          fxl::SplitStringCopy(test_exec, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

      FX_CHECK(!test_exec_args.empty()) << test_name << ": " << test_exec;
      test_commands_[test_name] = test_exec_args;

    } else if (test["exec"].IsArray()) {
      for (auto& arg : test["exec"].GetArray()) {
        FX_CHECK(arg.IsString()) << test_name;
        test_commands_[test_name].push_back(arg.GetString());
      }

      FX_CHECK(!test_commands_[test_name].empty()) << test_name;
      FX_CHECK(!test_commands_[test_name].front().empty()) << test_name;
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
