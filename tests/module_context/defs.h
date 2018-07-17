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

// The package name for the module under test.
constexpr char kModulePackageName[] = "module_context_test_module";

// The names of the first and second modules to be stopped.
constexpr char kFirstModuleName[] = "first";
constexpr char kSecondModuleName[] = "second";

// The signal the modules wait for before calling done.
constexpr char kFirstModuleCallDone[] = "first";
constexpr char kSecondModuleCallDone[] = "second";

// The signal the modules send when they have been terminated.
constexpr char kFirstModuleTerminated[] = "first terminated";
constexpr char kSecondModuleTerminated[] = "second terminated";

}  // namespace

#endif  // PERIDOT_TESTS_MODULE_CONTEXT_DEFS_H_
