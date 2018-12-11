// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_LINK_PASSING_DEFS_H_
#define PERIDOT_TESTS_LINK_PASSING_DEFS_H_

namespace {

// Package URLs of the test components used here.
constexpr char kModule2Url[] =
    "fuchsia-pkg://fuchsia.com/link_passing_test_module2#meta/link_passing_test_module2.cmx";
constexpr char kModule2Action[] = "module2_action";

constexpr char kModule3Url[] =
    "fuchsia-pkg://fuchsia.com/link_passing_test_module3#meta/link_passing_test_module3.cmx";
constexpr char kModule3Action[] = "module3_action";

}  // namespace

#endif  // PERIDOT_TESTS_LINK_PASSING_DEFS_H_
