// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_

#include <unordered_map>

namespace run {

enum EnvironmentType { SYS };

// Vars for configuring which environment particular components should run in.
constexpr char kLoggerTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/logger_integration_go_tests.cmx";
constexpr char kAppmgrHubTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/appmgr_hub_integration_tests.cmx";

const auto kUrlMap = new std::unordered_map<std::string, run::EnvironmentType>(
    {{kLoggerTestsUrl, run::EnvironmentType::SYS},
     {kAppmgrHubTestsUrl, run::EnvironmentType::SYS}});

class EnvironmentConfig {
 public:
  const std::unordered_map<std::string, EnvironmentType>* url_map() const { return kUrlMap; }
};
}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
