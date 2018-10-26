// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_PARENT_CHILD_DEFS_H_
#define PERIDOT_TESTS_PARENT_CHILD_DEFS_H_

namespace {

constexpr char kChildModuleName[] = "child";

// Package URLs of the test components used here.
constexpr char kChildModuleUrl1[] = "parent_child_test_child_module1";
constexpr char kChildModuleUrl2[] = "parent_child_test_child_module2";
constexpr char kChildModuleAction[] = "parent_child_test_child_module_action";

constexpr int kTimeoutMilliseconds = 5000;

}  // namespace

#endif  // PERIDOT_TESTS_PARENT_CHILD_DEFS_H_
