// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_

#include <map>
#include <string>

#include <fuchsia/crash/cpp/fidl.h>

namespace fuchsia {
namespace crash {

// Returns the default annotations we want in a crash report for native
// exceptions and kernel panics.
//
// Most annotations are shared between userspace and kernel crashes.
// Add additional arguments to this function for values that differ between the
// two, e.g., the package name can be extracted from the crashing process in
// userspace, but it's just "kernel" in kernel space.
std::map<std::string, std::string> MakeDefaultAnnotations(
    const std::string& package_name);

// Returns the annotations we want in a crash report for managed runtime
// exceptions.
//
// May augment the default annotation map from MakeDefaultAnnotations() or
// simply return the default.
std::map<std::string, std::string> MakeManagedRuntimeExceptionAnnotations(
    const std::string& component_url, ManagedRuntimeException* exception);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
