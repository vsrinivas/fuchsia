// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_COMMON_DEFS_H_
#define PERIDOT_TESTS_COMMON_DEFS_H_

namespace {

constexpr char kCommonNullModule[] =
    "fuchsia-pkg://fuchsia.com/common_null_module#meta/common_null_module.cmx";
constexpr char kCommonNullAction[] = "com.google.fuchsia.common.null";

constexpr char kCommonNullModuleStarted[] = "common_null_module_started";
constexpr char kCommonNullModuleStopped[] = "common_null_module_stopped";

constexpr char kCommonActiveModule[] =
    "fuchsia-pkg://fuchsia.com/common_active_module#meta/common_active_module.cmx";
constexpr char kCommonActiveAction[] = "com.google.fuchsia.common.active";

constexpr char kCommonActiveModuleStarted[] = "common_active_module_started";
constexpr char kCommonActiveModuleOngoing[] = "common_active_module_ongoing";
constexpr char kCommonActiveModuleStopped[] = "common_active_module_stopped";

}  // namespace

#endif  // PERIDOT_TESTS_COMMON_DEFS_H_
