// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>

#include <string>

#include <gtest/gtest.h>

#include "garnet/bin/run_test_component/max_severity_config.h"

TEST(MaxSeverity, ValidConfigs) {
  std::string path = "/pkg/data/max_severity/valid";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_FALSE(config.HasError()) << config.Error();
  std::map<std::string, int32_t> expected = {
      {"valid-1-url-1", FX_LOG_ERROR},   {"valid-1-url-2", FX_LOG_WARNING},
      {"valid-1-url-3", FX_LOG_ERROR},   {"valid-2-url-1", FX_LOG_INFO},
      {"valid-2-url-2", FX_LOG_WARNING}, {"valid-2-url-3", FX_LOG_ERROR},
      {"valid-3-url-1", FX_LOG_DEBUG},   {"valid-3-url-2", FX_LOG_TRACE},
      {"valid-3-url-3", FX_LOG_FATAL}};
  ASSERT_EQ(expected, config.config());
}

TEST(MaxSeverityErrors, NoUrl) {
  std::string path = "/pkg/data/max_severity/no_url";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_TRUE(config.HasError());
  ASSERT_EQ(config.Error(), "no_url.json: 'url' not found");
}

TEST(MaxSeverityErrors, InvalidUrl) {
  std::string path = "/pkg/data/max_severity/invalid_url";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_TRUE(config.HasError());
  ASSERT_EQ(config.Error(), "invalid_url.json: 'url' is not a string");
}

TEST(MaxSeverityErrors, NoSeverity) {
  std::string path = "/pkg/data/max_severity/no_severity";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_TRUE(config.HasError());
  ASSERT_EQ(config.Error(), "no_severity.json: 'max_severity' not found");
}

TEST(MaxSeverityErrors, Invalideverity) {
  std::string path = "/pkg/data/max_severity/invalid_severity";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_TRUE(config.HasError());
  ASSERT_EQ(config.Error(),
            "invalid_severity.json: 'INVALID' is not a valid severity for some_url. Must be one "
            "of: [TRACE, DEBUG, INFO, WARN, ERROR, FATAL]");
}

TEST(MaxSeverityErrors, UrlConflict) {
  std::string path = "/pkg/data/max_severity/url_conflict";

  auto config = run::MaxSeverityConfig::ParseFromDirectory(path);
  ASSERT_TRUE(config.HasError());
  ASSERT_EQ(config.Error(), "url_conflict.json: test some_url configured twice.");
}
