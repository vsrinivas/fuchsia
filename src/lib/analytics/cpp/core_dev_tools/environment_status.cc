// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "environment_status.h"

#include <cstdlib>

namespace analytics::core_dev_tools {

namespace {

constexpr BotInfo kBotEnvironments[] = {{"TEST_ONLY_ENV", "test-only"},
                                        {"TF_BUILD", "azure"},
                                        {"bamboo.buildKey", "bamboo"},
                                        {"BUILDKITE", "buildkite"},
                                        {"CIRCLECI", "circle"},
                                        {"CIRRUS_CI", "cirrus"},
                                        {"CODEBUILD_BUILD_ID", "codebuild"},
                                        {"UNITTEST_ON_FORGE", "forge"},
                                        {"SWARMING_BOT_ID", "luci"},
                                        {"GITHUB_ACTIONS", "github"},
                                        {"GITLAB_CI", "gitlab"},
                                        {"HEROKU_TEST_RUN_ID", "heroku"},
                                        {"BUILD_ID", "hudson-jenkins"},
                                        {"TEAMCITY_VERSION", "teamcity"},
                                        {"TRAVIS", "travis"}};
}  // namespace

bool IsRunByBot() { return GetBotInfo().IsRunByBot(); }

BotInfo GetBotInfo() {
  for (const auto& bot : kBotEnvironments) {
    if (std::getenv(bot.environment)) {
      return bot;
    }
  }
  return {};
}

bool BotInfo::IsRunByBot() const { return environment != nullptr; }

}  // namespace analytics::core_dev_tools
