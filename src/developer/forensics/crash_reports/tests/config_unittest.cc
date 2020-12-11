// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/config.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace crash_reports {
namespace {

constexpr CrashServerConfig::UploadPolicy kDisabled = CrashServerConfig::UploadPolicy::DISABLED;
constexpr CrashServerConfig::UploadPolicy kEnabled = CrashServerConfig::UploadPolicy::ENABLED;
constexpr CrashServerConfig::UploadPolicy kReadFromPrivacySettings =
    CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;

class ConfigTest : public testing::Test {
 protected:
  std::string WriteConfig(const std::string& config) {
    std::string path;
    FX_CHECK(tmp_dir_.NewTempFileWithData(config, &path));
    return path;
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

// Parse |config_str| into |var| or assert if the operation fails.
#define PARSE_OR_ASSERT(var, config_str)                 \
  auto tmp_##var = ParseConfig(WriteConfig(config_str)); \
  ASSERT_TRUE(tmp_##var.has_value());                    \
  auto var = std::move(tmp_##var.value());

// Parse |config_str| and assert it is malformed.
#define ASSERT_IS_BAD_CONFIG(config_str) \
  ASSERT_FALSE(ParseConfig(WriteConfig(config_str)).has_value());

TEST_F(ConfigTest, ParseConfig_ValidConfig_UploadDisabled) {
  PARSE_OR_ASSERT(config, R"({
    "crash_server" : {
        "upload_policy": "disabled"
    }
})");
  EXPECT_EQ(config.crash_server.upload_policy, kDisabled);
}

TEST_F(ConfigTest, ParseConfig_ValidConfig_UploadEnabled) {
  PARSE_OR_ASSERT(config, R"({
    "crash_server" : {
        "upload_policy": "enabled"
    }
})");
  EXPECT_EQ(config.crash_server.upload_policy, kEnabled);
}

TEST_F(ConfigTest, ParseConfig_ValidConfig_UploadReadFromPrivacySettings) {
  PARSE_OR_ASSERT(config, R"({
    "crash_server" : {
        "upload_policy": "read_from_privacy_settings"
    }
})");
  EXPECT_EQ(config.crash_server.upload_policy, kReadFromPrivacySettings);
}

TEST_F(ConfigTest, ParseConfig_HasDailyPerProductQuota) {
  PARSE_OR_ASSERT(config, R"({
    "crash_reporter": {
        "daily_per_product_quota": 100
    },
    "crash_server" : {
        "upload_policy": "enabled"
    }
})");
  ASSERT_TRUE(config.daily_per_product_quota.has_value());
  EXPECT_EQ(config.daily_per_product_quota.value(), 100u);
}

TEST_F(ConfigTest, ParseConfig_MissingDailyPerProductQuota) {
  PARSE_OR_ASSERT(config, R"({
    "crash_server" : {
        "upload_policy": "enabled"
    }
})");
  EXPECT_FALSE(config.daily_per_product_quota.has_value());
}

TEST_F(ConfigTest, ParseConfig_MissingConfig) {
  ASSERT_FALSE(ParseConfig("undefined file").has_value());
}

TEST_F(ConfigTest, ParseConfig_BadConfig_SpuriousField) {
  ASSERT_IS_BAD_CONFIG(R"({
    "crash_server" : {
        "upload_policy": "disabled"
    },
    "spurious field": []
})");
}

TEST_F(ConfigTest, ParseConfig_BadConfig_MissingRequiredField) {
  ASSERT_IS_BAD_CONFIG(R"({
})");
}

TEST_F(ConfigTest, ParseConfig_BadConfig_InvalidUploadPolicy) {
  ASSERT_IS_BAD_CONFIG(R"({
    "crash_server" : {
        "upload_policy": "not_in_enum"
    }
})");
}

TEST_F(ConfigTest, ParseConfig_HourlySnapshots) {
  {
    PARSE_OR_ASSERT(config, R"({
       "crash_server" : {
           "upload_policy": "enabled"
       },
       "hourly_snapshot": true
   })");
    EXPECT_TRUE(config.hourly_snapshot);
  }
  {
    PARSE_OR_ASSERT(config, R"({
       "crash_server" : {
           "upload_policy": "enabled"
       },
       "hourly_snapshot": false
   })");
    EXPECT_FALSE(config.hourly_snapshot);
  }
  {
    PARSE_OR_ASSERT(config, R"({
    "crash_server" : {
        "upload_policy": "enabled"
    }
})");
    EXPECT_FALSE(config.hourly_snapshot);
  }
}

}  // namespace

// Pretty-prints CrashServerConfig::UploadPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const CrashServerConfig::UploadPolicy& upload_policy, std::ostream* os) {
  *os << ToString(upload_policy);
}

#undef ASSERT_IS_BAD_CONFIG
#undef PARSE_OR_ASSERT

}  // namespace crash_reports
}  // namespace forensics
