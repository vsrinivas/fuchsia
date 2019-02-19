// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_INTENTS_DEFS_H_
#define PERIDOT_TESTS_INTENTS_DEFS_H_

namespace {

constexpr char kChildModuleName[] = "child";

// Package URLs of the test components used here.
constexpr char kChildModuleUrl[] =
    "fuchsia-pkg://fuchsia.com/intent_test_child_module#meta/"
    "intent_test_child_module.cmx";

// The action of the intent the parent module issues to the child module.
constexpr char kChildModuleAction[] = "intent_test_child_module_action";

// The signal which the child module sends when it has received an intent.
constexpr char kChildModuleHandledIntent[] = "child_module_handled_intent";

// The name of the intent parameter which contains a string which the child
// module is to append to its signal.
constexpr char kIntentParameterName[] = "intent_parameter";

// The name of the intent parameter which contains a string which the child
// module is to append to its signal.
constexpr char kIntentParameterNameAlternate[] = "intent_parameter_alternate";

constexpr int kTimeoutMilliseconds = 5000;

}  // namespace

#endif  // PERIDOT_TESTS_INTENTS_DEFS_H_
