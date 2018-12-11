// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_EMBED_SHELL_DEFS_H_
#define PERIDOT_TESTS_EMBED_SHELL_DEFS_H_

namespace {

constexpr char kChildModuleName[] = "child";

// Package URLs of the test components used here.
constexpr char kChildModuleUrl[] =
    "fuchsia-pkg://fuchsia.com/embed_shell_test_child_module#meta/embed_shell_test_child_module.cmx";

constexpr char kChildModuleAction[] = "action";

}  // namespace

#endif  // PERIDOT_TESTS_EMBED_SHELL_DEFS_H_
