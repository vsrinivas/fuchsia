// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_TRIGGER_DEFS_H_
#define PERIDOT_TESTS_TRIGGER_DEFS_H_

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 10000;

// Package URLs of the test components used here.
constexpr char kTestAgent[] =
    "fuchsia-pkg://fuchsia.com/trigger_test_agent#meta/trigger_test_agent.cmx";

const char kModuleUrl[] =
    "fuchsia-pkg://fuchsia.com/trigger_test_module#meta/trigger_test_module.cmx";

const char kModuleAction[] = "trigger_test_action";

}  // namespace

#endif  // PERIDOT_TESTS_TRIGGER_DEFS_H_
