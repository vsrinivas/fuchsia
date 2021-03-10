// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "environment_status.h"

#include <cstdlib>

namespace analytics::core_dev_tools {

namespace {

constexpr const char* const kBotEnvironments[] = {
    "TF_BUILD",            // Azure
    "bamboo.buildKey",     // Bamboo
    "BUILDKITE",           // BUILDKITE
    "CIRCLECI",            // Circle
    "CIRRUS_CI",           // Cirrus
    "CODEBUILD_BUILD_ID",  // Codebuild
    "UNITTEST_ON_FORGE",   // Forge
    "SWARMING_BOT_ID",     // Fuchsia
    "GITHUB_ACTIONS",      // GitHub Actions
    "GITLAB_CI",           // GitLab
    "HEROKU_TEST_RUN_ID",  // Heroku
    "BUILD_ID",            // Hudson & Jenkins
    "TEAMCITY_VERSION",    // Teamcity
    "TRAVIS",              // Travis
};

}  // namespace

bool IsRunByBot() {
  for (const char* env : kBotEnvironments) {
    if (std::getenv(env)) {
      return true;
    }
  }
  return false;
}

}  // namespace analytics::core_dev_tools
