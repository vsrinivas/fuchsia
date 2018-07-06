// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"

namespace sysmgr {
namespace {

using fxl::StringPrintf;
using testing::AllOf;
using testing::ElementsAre;
using testing::Key;
using testing::UnorderedElementsAre;
using testing::Value;

TEST(ConfigTest, FailsIfEmpty) {
  Config config;
  config.Parse("", "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(), ElementsAre("test: The document is empty."));
  EXPECT_EQ(config.GetFailedConfig(), "");
}

TEST(ConfigTest, InvalidValue) {
  Config config;
  config.Parse("3", "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(),
              ElementsAre("test: Config file is not a JSON object"));
  EXPECT_EQ(config.GetFailedConfig(), "3");
}

TEST(ConfigTest, ParseErrorWithLine) {
  const auto kTestCase = R"json({
  "services": "missing closing quote,
  })json";

  Config config;
  config.Parse(kTestCase, "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(),
              ElementsAre("test:2:38 Invalid encoding in string."));
  EXPECT_EQ(config.GetFailedConfig(), kTestCase);
}

TEST(ConfigTest, ServicesError) {
  const auto kTestCase = R"json({
    "services": {
      "chrome": 3,
      "appmgr": [], 
      "other": ["a", 3]
    }})json";
  const auto kErrorFormat =
      "test: %s must be a string or a non-empty array of strings";

  Config config;
  config.Parse(kTestCase, "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(),
              ElementsAre(StringPrintf(kErrorFormat, "services.chrome"),
                          StringPrintf(kErrorFormat, "services.appmgr"),
                          StringPrintf(kErrorFormat, "services.other")));
  EXPECT_EQ(config.GetFailedConfig(), kTestCase);
}

TEST(ConfigTest, AppsError) {
  const auto kTestCase = R"json({"apps": 3})json";

  Config config;
  config.Parse(kTestCase, "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(),
              ElementsAre("test: apps value is not an array"));
  EXPECT_EQ(config.GetFailedConfig(), kTestCase);
}

TEST(ConfigTest, StartupServicesError) {
  const auto kTestCase = R"json({"startup_services": [3, "33"]})json";

  Config config;
  config.Parse(kTestCase, "test");
  EXPECT_TRUE(config.HasErrors());
  EXPECT_THAT(config.GetErrors(),
              ElementsAre("test: startup_services is not an array of strings"));
  EXPECT_EQ(config.GetFailedConfig(), kTestCase);
}

TEST(ConfigTest, ValidConfig) {
  const auto kTestCaseServices = R"json({
    "services": {
      "fuchsia.logger.Log": "logger",
      "fuchsia.Debug": ["debug", "arg1"]
    },
    "startup_services": ["fuchsia.logger.Log"]
  })json";

  const auto kTestCaseApps = R"json({
    "apps": [
      "netconnector",
      ["listen", "22"]
    ],
    "loaders": {
      "http": "network_loader"
    }
  })json";

  Config config;

  config.Parse(kTestCaseServices, "test");
  EXPECT_FALSE(config.HasErrors());
  EXPECT_EQ(config.GetFailedConfig(), "");

  config.Parse(kTestCaseApps, "test");
  EXPECT_FALSE(config.HasErrors());
  EXPECT_EQ(config.GetFailedConfig(), "");

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
