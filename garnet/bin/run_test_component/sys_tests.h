// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_

#include <unordered_set>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace run {

constexpr char kLoggerTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/logger_integration_go_tests.cmx";
constexpr char kAppmgrHubTestsUrl[] =
    "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/appmgr_hub_integration_tests.cmx";
// System test that needs access to the system time zone service.
constexpr char kTimezoneTestUrl[] =
    "fuchsia-pkg://fuchsia.com/timezone-test#meta/timezone_bin_test.cmx";
constexpr char kDevicePropertySmokeTestUrl[] =
    "fuchsia-pkg://fuchsia.com/device-property-smoke-test#meta/device_property_smoke_test.cmx";

const std::unordered_set<std::string> kUrlSet({
    {kLoggerTestsUrl},
    {kAppmgrHubTestsUrl},
    {kTimezoneTestUrl},
    {kDevicePropertySmokeTestUrl},
});

// Returns true if this test should be executed in 'sys' environment.
bool should_run_in_sys(const std::string& url);

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_
