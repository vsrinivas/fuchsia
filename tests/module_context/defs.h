// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_MODULE_CONTEXT_DEFS_H_
#define PERIDOT_TESTS_MODULE_CONTEXT_DEFS_H_

namespace {

// The name of the link that the modules receive their startup parameters on.
constexpr char kLinkName[] = "link";

// The key the modules use to Get their name.
constexpr char kLinkKey[] = "name";

// The URL for the module under test.
constexpr char kModuleUrl[] =
    "fuchsia-pkg://fuchsia.com/module_context_test_module#meta/module_context_test_module.cmx";

// The intent action |kModuleUrl| accepts.
constexpr char kIntentAction[] = "action";

// The names of the first and second modules to be stopped.
constexpr char kFirstModuleName[] = "first";
constexpr char kSecondModuleName[] = "second";

// The signal the modules wait for before calling done.
constexpr char kFirstModuleCallDone[] = "first_done";
constexpr char kSecondModuleCallDone[] = "second_done";

// The signal the modules wait for before starting/stopping ongoing activity.
constexpr char kFirstModuleCallStartActivity[] = "first_activity_start";
constexpr char kSecondModuleCallStartActivity[] = "second_activity_start";
constexpr char kFirstModuleCallStopActivity[] = "first_activity_stop";
constexpr char kSecondModuleCallStopActivity[] = "second_activity_stop";

// The signal the modules send when they have been terminated.
constexpr char kFirstModuleTerminated[] = "first terminated";
constexpr char kSecondModuleTerminated[] = "second terminated";

// The signals the entity module sends once it's done verifying the entity
// related methods on module context. Once all these have been signaled the
// module is done.
constexpr char kEntityModuleDoneFirstTask[] = "entity_module_done_first";
constexpr char kEntityModuleDoneSecondTask[] = "entity_module_done_second";

}  // namespace

#endif  // PERIDOT_TESTS_MODULE_CONTEXT_DEFS_H_
