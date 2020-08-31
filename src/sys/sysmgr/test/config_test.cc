// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/config.h"

#include <stdio.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace sysmgr {
namespace {

using testing::AllOf;
using testing::ElementsAre;
using testing::Key;
using testing::UnorderedElementsAre;
using testing::Value;

class ConfigTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, std::string expected_error) {
    Config config;
    std::string dir;
    ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
    const std::string json_file = NewJSONFile(dir, json);
    EXPECT_FALSE(config.ParseFromDirectory(dir));
    EXPECT_THAT(config.error_str(), ::testing::HasSubstr(expected_error));
  }

  std::string NewJSONFile(const std::string& dir, const std::string& json) {
    const std::string json_file =
        fxl::Substitute("$0/json_file$1", dir, std::to_string(unique_id_++));
    ZX_ASSERT(files::WriteFile(json_file, json.data(), json.size()));
    return json_file;
  }

  files::ScopedTempDir tmp_dir_;

 private:
  int unique_id_ = 1;
};

TEST_F(ConfigTest, ParseWithErrors) {
  std::string json;

  // Empty document.
  json = "";
  ExpectFailedParse(json, "The document is empty.");

  // Document is not an object.
  json = "3";
  ExpectFailedParse(json, "Config file is not a JSON object.");

  // Bad services.
  constexpr char kBadServiceError[] = "'$0' must be a string or a non-empty array of strings.";
  json = R"json({
  "services": {
    "chrome": 3,
    "appmgr": [],
    "other": ["a", 3]
  }})json";
  {
    std::string dir;
    ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
    const std::string json_file = NewJSONFile(dir, json);
    Config config;
    EXPECT_FALSE(config.ParseFromDirectory(dir));
    EXPECT_THAT(config.error_str(),
                ::testing::HasSubstr(fxl::Substitute(kBadServiceError, "services.chrome")));
    EXPECT_THAT(config.error_str(),
                ::testing::HasSubstr(fxl::Substitute(kBadServiceError, "services.appmgr")));
    EXPECT_THAT(config.error_str(),
                ::testing::HasSubstr(fxl::Substitute(kBadServiceError, "services.other")));
  }

  // Bad apps.
  json = R"json({"apps": 3})json";
  ExpectFailedParse(json, "'apps' is not an array.");

  // Bad startup services.
  json = R"json({"startup_services": [3, "33"]})json";
  ExpectFailedParse(json, "'startup_services' is not an array of strings.");
}

TEST_F(ConfigTest, Parse) {
  constexpr char kServices[] = R"json({
    "services": {
      "fuchsia.logger.Log": "logger",
      "fuchsia.Debug": ["debug", "arg1"]
    },
    "startup_services": ["fuchsia.logger.Log"],
    "optional_services": ["fuchsia.tracing.controller.Controller"]
  })json";
  constexpr char kApps[] = R"json({
    "apps": [
      "netconnector",
      ["listen", "22"]
    ]
  })json";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kServices);
  NewJSONFile(dir, kApps);

  Config config;
  EXPECT_TRUE(config.ParseFromDirectory(dir));
  EXPECT_FALSE(config.HasError());
  EXPECT_EQ(config.error_str(), "");

  auto services = config.TakeServices();
  EXPECT_THAT(services, UnorderedElementsAre(Key("fuchsia.Debug"), Key("fuchsia.logger.Log")));
  EXPECT_THAT(*services["fuchsia.Debug"]->arguments, ElementsAre("arg1"));

  auto apps = config.TakeApps();
  EXPECT_EQ(apps[0]->url, "netconnector");
  EXPECT_EQ(apps[1]->url, "listen");
  EXPECT_THAT(*apps[1]->arguments, ElementsAre("22"));

  auto startup_services = config.TakeStartupServices();
  EXPECT_THAT(startup_services, ElementsAre("fuchsia.logger.Log"));

  auto optional_services = config.TakeOptionalServices();
  EXPECT_THAT(optional_services, ElementsAre("fuchsia.tracing.controller.Controller"));
}

TEST_F(ConfigTest, FailWhenDuplicateDetected) {
  constexpr char kServices[] = R"json({
    "services": {
      "fuchsia.logger.Log": "logger",
      "fuchsia.logger.Log": "logger_duplicated",
      "fuchsia.Debug": ["debug", "arg1"]
    }
  })json";
  constexpr char kApps[] = R"json({
    "services": {
      "fuchsia.some.Service": "fuchsia-pkg://some/package",
      "fuchsia.Debug": "fuchsia-pkg://some/duplicate/implementation"
    }
  })json";

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kServices);
  NewJSONFile(dir, kApps);

  Config config;
  EXPECT_FALSE(config.ParseFromDirectory(dir));
  EXPECT_EQ(config.error_str(),
            "json_file1: Duplicate definition in map for 'services': fuchsia.logger.Log\n"
            "json_file2: Duplicate definition in map for 'services': fuchsia.Debug");
  EXPECT_TRUE(config.HasError());
}

TEST_F(ConfigTest, CriticalComponents) {
  constexpr char kServices[] = R"json({
    "services": {
      "fuchsia.logger.Log": "logger",
      "fuchsia.Debug": ["debug", "arg1"]
    },
    "startup_services": ["fuchsia.logger.Log"],
    "optional_services": ["fuchsia.tracing.controller.Controller"]
  })json";
  constexpr char kCriticalComponents[] = R"json({
    "critical_components": ["logger"]
  })json";
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kServices);
  NewJSONFile(dir, kCriticalComponents);

  Config config;
  EXPECT_TRUE(config.ParseFromDirectory(dir));
  EXPECT_EQ(std::vector<std::string>{"logger"}, config.TakeCriticalComponents());
}

}  // namespace
}  // namespace sysmgr
