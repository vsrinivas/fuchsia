// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <string>

namespace fuchsia {
namespace crash {

// Returns the default annotations we want in all crash reports.
//
// |feedback_data| may contain annotations that are shared with other
// feedback reports, e.g., user feedback reports.
//
// Most annotations are shared between userspace and kernel crashes.
// Add additional arguments to this function for values that differ between the
// two, e.g., the package name can be extracted from the crashing process in
// userspace, but it's just "kernel" in kernel space.
std::map<std::string, std::string> MakeDefaultAnnotations(
    const fuchsia::feedback::Data& feedback_data,
    const std::string& package_name);

// Returns the annotations we want in a crash report for managed runtime
// exceptions.
//
// May augment the default annotation map from MakeDefaultAnnotations() or
// simply return the default.
std::map<std::string, std::string> MakeManagedRuntimeExceptionAnnotations(
    const fuchsia::feedback::Data& feedback_data,
    const std::string& component_url, ManagedRuntimeException* exception);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
