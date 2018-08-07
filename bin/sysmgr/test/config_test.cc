// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string>

#include "garnet/bin/sysmgr/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/substitute.h"

namespace sysmgr {
namespace {

using fxl::StringPrintf;
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
    EXPECT_EQ(config.error_str(), fxl::Substitute(expected_error, json_file));
  }

  std::string NewJSONFile(const std::string& dir, const std::string& json) {
    const std::string json_file =
        fxl::Substitute("$0/json_file$1", dir, std::to_string(unique_id_++));
    if (!files::WriteFile(json_file, json.data(), json.size())) {
      return "";
    }
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
  ExpectFailedParse(json, "$0: The document is empty.");

  // Document is not an object.
  json = "3";
  ExpectFailedParse(json, "$0: Config file is not a JSON object.");

  // Bad services.
  constexpr char kBadServiceError[] =
      "$0: '%s' must be a string or a non-empty array of strings.";
  json = R"json({
  "services": {
    "chrome": 3,
    "appmgr": [],
    "other": ["a", 3]
  }})json";
  ExpectFailedParse(
      json, StringPrintf(kBadServiceError, "services.chrome") + "\n" +
                StringPrintf(kBadServiceError, "services.appmgr") + "\n" +
                StringPrintf(kBadServiceError, "services.other"));

  // Bad apps.
  json = R"json({"apps": 3})json";
  ExpectFailedParse(json, "$0: 'apps' is not an array.");

  // Bad startup services.
  json = R"json({"startup_services": [3, "33"]})json";
  ExpectFailedParse(json, "$0: 'startup_services' is not an array of strings.");
}

TEST_F(ConfigTest, Parse) {
  constexpr char kServices[] = R"json({
    "services": {
      "fuchsia.logger.Log": "logger",
      "fuchsia.Debug": ["debug", "arg1"]
    },
    "startup_services": ["fuchsia.logger.Log"]
  })json";
  constexpr char kApps[] = R"json({
    "apps": [
      "netconnector",
      ["listen", "22"]
    ],
    "loaders": {
      "http": "network_loader"
    }
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
  EXPECT_THAT(services, UnorderedElementsAre(Key("fuchsia.Debug"),
                                             Key("fuchsia.logger.Log")));
  EXPECT_THAT(*services["fuchsia.Debug"]->arguments, ElementsAre("arg1"));

  auto apps = config.TakeApps();
  EXPECT_EQ(apps[0]->url, "netconnector");
  EXPECT_EQ(apps[1]->url, "listen");
  EXPECT_THAT(*apps[1]->arguments, ElementsAre("22"));

  auto startup_services = config.TakeStartupServices();
  EXPECT_THAT(startup_services, ElementsAre("fuchsia.logger.Log"));

  auto loaders = config.TakeAppLoaders();
  EXPECT_THAT(loaders, UnorderedElementsAre(Key("http")));
  EXPECT_EQ(*loaders["http"]->url, "network_loader");
}

}  // namespace
}  // namespace sysmgr
