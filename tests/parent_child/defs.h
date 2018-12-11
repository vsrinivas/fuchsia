// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_PARENT_CHILD_DEFS_H_
#define PERIDOT_TESTS_PARENT_CHILD_DEFS_H_

namespace {

constexpr char kChildModuleName[] = "child";

// Package URLs of the test components used here.
constexpr char kChildModuleUrl1[] =
    "fuchsia-pkg://fuchsia.com/parent_child_test_child_module1#meta/parent_child_test_child_module1.cmx";
constexpr char kChildModuleUrl2[] =
    "fuchsia-pkg://fuchsia.com/parent_child_test_child_module2#meta/parent_child_test_child_module2.cmx";
constexpr char kChildModuleAction[] =
    "fuchsia-pkg://fuchsia.com/parent_child_test_child_module_action#meta/parent_child_test_child_module_action.cmx";

constexpr int kTimeoutMilliseconds = 5000;

}  // namespace

#endif  // PERIDOT_TESTS_PARENT_CHILD_DEFS_H_
