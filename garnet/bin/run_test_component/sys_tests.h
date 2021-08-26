// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_

#include <unordered_set>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace run {

// System test that needs access to the system time zone service.
constexpr char kTimezoneTestUrl[] =
    "fuchsia-pkg://fuchsia.com/timezone-test#meta/timezone_bin_test.cmx";
constexpr char kTimezoneFlutterTestUrl[] =
    "fuchsia-pkg://fuchsia.com/timezone-flutter-test#meta/timezone_flutter_bin_test.cmx";

const std::unordered_set<std::string> kUrlSet({
    {kTimezoneTestUrl},
    {kTimezoneFlutterTestUrl},
});

// Returns true if this test should be executed in 'sys' environment.
bool should_run_in_sys(const std::string& url);

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_SYS_TESTS_H_
